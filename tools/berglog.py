#!/usr/bin/env python3
"""berglog.py -- offline analyser for MeshCom firmware console logs of mountain gateways.

Usage
-----
    python3 tools/berglog.py <log>... --out <dir>

Writes ``berg.json`` (every number) and ``berg.md`` (human readable tables)
into ``<dir>``.  Stdlib only, no third-party dependencies.


Log line anatomy
----------------
Every line carries a host-side capture prefix written by ``tools/meshlogger.py``::

    YYYY-MM-DD HH:MM:SS.mmm  <node payload>

The host clock is NTP-synced and is CEST (UTC+2) for these captures -- verified
against the ``[NTP];ok;epoch;<unix>`` lines, whose epoch equals host time - 2 h.
All timing in this analyser uses the HOST timestamp; the node clock printed
inside the payload has 1 s granularity and lags the host stamp by 0..2 s
(decode + print latency).


Receive lines -- ``printBuffer_aprs()``, src/loop_functions.cpp:3144
-------------------------------------------------------------------
::

    HH:MM:SS [LOG] LLL T xIIIIIIII HHH Ss Tt Mmm PATH>DESTTPAYLOAD \
             HW:dd MOD:c/m FCS:ffff FW:vv:s LH:hh

    LLL      msg_len, decimal, 3 digits -- the LoRa PHY payload length in bytes
    T        payload_type: ':' text, '@' HEY/heartbeat, '!' position.  A line with
             ``FCS:0000`` is a decode failure -- ``decodeAPRS()`` bailed and the
             struct still holds its ``initAPRS()`` defaults, so path/dest/payload
             are empty or truncated and the type char is garbage ('.', '0', 'P',
             'T', or a plausible-looking one).  Such lines are counted separately
             and excluded from every message statistic.
    xIIIIIIII msg_id, 8 hex digits, network-wide message identity
    Hhh      max_hop REMAINING, 2 hex digits; decremented by every relay.
             The originator's configured hop limit is therefore
             ``max_hop + hops_taken``.
    Ss       msg_server flag (0/1)
    Tt       msg_track flag (0/1)
    Mmm      msg_mesh flag, 2 hex digits
    PATH     comma separated callsigns.  First = originator, last = the direct
             RF neighbour that handed the frame to this node (== originator when
             the frame was heard directly).  ``hops_taken = len(path) - 1``.
    DEST     destination path, e.g. ``*`` (broadcast), ``20`` (group 20),
             ``100001`` (telemetry group), ``H``/``HG`` (HEY: ``HG`` = the
             originator is a gateway, ``H`` = plain node), or a callsign for a DM.
    HW:dd    msg_source_hw, DECIMAL hardware id of the ORIGINATOR
    MOD:c/m  c = node_country nibble, m = modulation index.  ``8/8`` = country
             "EU preamble 8" and MeshCom modulation 8 = SF11 / CR 4/6 / BW 250k
             (src/lora_setchip.cpp:169 getMOD, case 8 of lora_setcountry).
    FCS:ffff frame check sum as printed by the sender
    FW:vv:s  vv = firmware major (35 -> 4.35), s = sub version letter.
             ``FW:35:p`` = 4.35p, the first release with CSMA/CAD.  Letters
             c,d,e,f,h,i,j,k,m,n,o and majors < 35 predate CAD and transmit
             without a channel check.  ``#`` = sub version byte 0x00.
    LH:hh    msg_last_hw, 2 hex digits.  Bit 7 is the "last sending" flag; the
             low 7 bits are the hardware id of the LAST HOP (the direct
             neighbour).  Ids resolve against the ``//Hardware Types`` block in
             src/configuration_global.h.

The ``[LOG]`` line is printed for EVERY decoded frame, before dedup and before
the own-transmission check (src/lora_functions.cpp:576-582), so one line is
emitted per RECEPTION -- the same msg_id appears once per copy heard.  The line
carries no RSSI/SNR.

7- and 12-byte ACK frames use ``printBuffer_ack()`` instead
(src/loop_functions.cpp:3151) and print as ``[LOG] 007 ...`` / ``[LOG] 012 ...``.


HEY ('@') payload grammar
-------------------------
``sendHey()`` (src/loop_functions.cpp:4788) emits destination ``HG`` for a
gateway and ``H`` otherwise, with payload ``"R" + mheard_count + ";"``.  Every
relay appends one group via ``appendHeySignalReport()``
(src/aprs_functions.cpp:1161)::

    <mheard_count>,<-RSSI>,<SNR>;

Older firmware emits a two-field group ``<-RSSI>,<SNR>;`` instead, and older
ORIGINATORS emit a bare ``R`` with no count at all, so the first relay group sits
directly behind the ``R``.  Both dialects were confirmed against the frames heard
DIRECT from their originator (path length 1), where the payload is untouched: the
only two shapes that occur are ``R`` (190 receptions) and ``R<n>;`` (165).  Hence
``R3;26,110,-14;114,-11;`` (count 3, then two relay reports) and
``R131,-13;16,103,-17;...`` (no count, first relay report ``131,-13``).  This
analyser strips the leading ``R``, splits on ``;`` and treats the first element as
the originator's count only when it carries no comma.  The number of relay groups
is expected to equal ``len(path) - 1``; a shorter chain means some relay forwarded
the frame without appending its report.


Other line families
-------------------
``[GW];keep;tx;ok;n;ms;t``          gateway keepalive to the server
``[GW];rx;type;BEAT|CET;len;n;ms;t`` server frame received over UDP
``[ETH];link;up;...``               periodic Ethernet state with counters
                                    (downs, renews, renew_fail, resets, tx_fail, ...)
``[ETH];stall;<what>;ms;n;task;t``  a blocking Ethernet section
``[ETH];event;dhcp|link;rc;n;ms;t`` DHCP / link events
``[NTP];ok;epoch;e;rtt;r`` / ``[NTP];timeout``
``[INSTR-LOOP] gap ms N in <sec> section_ms A sections_ms B``  main-loop stall
``HH:MM:SS;[HEAP];free;free_min;largest;(mon)``
``[GATE] Received a LoRa packet to transmit``  server -> LoRa injection
``RX-UDP Source-Path:...`` / ``RX-UDP Check-payload (n):hh``
``APRS decode - Packet discarded, wrong FW-version <path><ver>!``
``[UDP_ETH] ...``, ``UDP Out Buff:...``, ``LOOP GATEWAY actions UDP received``,
``HH:MM:SS [BEAT] Heartbeat from server``


Own-callsign detection
----------------------
A node never receives its own transmission, so its own callsign never appears as
the LAST element of a received path.  It does appear one position earlier
whenever a neighbour relays a frame this node had just relayed.  The analyser
picks the callsign with the highest penultimate-position count and a
last-position count below 5 % of it.  A log without ``[LOG]`` lines (receive log
flag off) yields no own callsign.


Airtime model
-------------
MOD 8 = SF11, BW 250 kHz, CR 4/6, 8 symbol preamble, explicit header, CRC on,
no low-data-rate optimisation (symbol time 8.192 ms < 16 ms).  Time on air is
the standard Semtech LoRa formula applied to ``msg_len``.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from collections import Counter, defaultdict
from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

#: ``//Hardware Types`` block of src/configuration_global.h.
HARDWARE_TYPES: dict[int, str] = {
    1: "TLORA_V2",
    2: "TLORA_V1",
    3: "TLORA_V2_1_1p6",
    4: "TBEAM",
    5: "TBEAM_1268",
    6: "TBEAM_0p7",
    7: "T_ECHO",
    8: "T_DECK",
    9: "RAK4631",
    10: "HELTEC_V2_1",
    11: "HELTEC_V1",
    12: "TBEAM_AXP2101",
    39: "EBYTE_E22",
    40: "T5_EPAPER",
    41: "HELTEC_TRACKER",
    42: "HELTEC_STICK_V3",
    43: "HELTEC_V3",
    44: "HELTEC_E290",
    45: "TBEAM_1262",
    46: "T_DECK_PLUS",
    47: "TBEAM_SUPREME",
    48: "ESP32_S3_EBYTE_E22",
    49: "TLORA_PAGER",
    50: "T_DECK_PRO",
    51: "TBEAM_1W",
    52: "HELTEC_V4",
    53: "T_ETH_ELITE_1262",
    54: "HELTEC_T114",
    55: "T3_S3_V13",
    56: "T_CONNECT_PRO",
    57: "HELTEC_WIRELESS_PAPER",
    58: "HELTEC_E213",
    59: "ESP32_LORAPRS_E22",
    60: "ESP32_LORAPRS_RA01",
    61: "T_WATCH_S3",
}

#: Firmware sub-version letters that already carry CSMA/CAD.
CAD_FW_LETTERS: frozenset[str] = frozenset({"p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"})

#: Callsigns of the Internet-injected server time signal.
TIME_SIGNAL_CALLS: frozenset[str] = frozenset({"OE1XAR-33", "OE1XAR-62"})

#: Host capture clock offset against the UTC time carried in ``{CET}`` beacons.
CET_HOST_OFFSET = timedelta(hours=2)

TYPE_LABEL: dict[str, str] = {
    ":": "text",
    "@": "heartbeat (HEY)",
    "!": "position",
}

BUCKET_MINUTES = 10

# --------------------------------------------------------------------------
# Regular expressions
# --------------------------------------------------------------------------

RE_PREFIX = re.compile(r"^(?P<host>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s\s(?P<rest>.*)$")

RE_APRS = re.compile(
    r"^(?P<nodetime>\d{2}:\d{2}:\d{2}) \[LOG\] (?P<len>\d{3}) (?P<type>.) "
    r"x(?P<msgid>[0-9A-Fa-f]{8}) H(?P<hop>[0-9A-Fa-f]{2}) S(?P<s>\d) T(?P<t>\d) M(?P<m>[0-9A-Fa-f]{2}) "
    r"(?P<body>.*) HW:(?P<hw>\d+) MOD:(?P<modc>[0-9A-Fa-f])/(?P<modm>\d) "
    r"FCS:(?P<fcs>[0-9A-Fa-f]{4}) FW:(?P<fwmaj>\d+):(?P<fwsub>.) LH:(?P<lh>[0-9A-Fa-f]{2})$"
)

RE_ACK7 = re.compile(
    r"^(?P<nodetime>\d{2}:\d{2}:\d{2}) \[LOG\] 007 (?P<type>.) x(?P<msgid>[0-9A-Fa-f]{8}) "
    r"H(?P<hop>[0-9A-Fa-f]{2}) (?P<b6>[0-9A-Fa-f]{2})$"
)
RE_ACK12 = re.compile(
    r"^(?P<nodetime>\d{2}:\d{2}:\d{2}) \[LOG\] 012 (?P<type>.) x(?P<msgid>[0-9A-Fa-f]{8}) "
    r"H(?P<hop>[0-9A-Fa-f]{2}) x(?P<msgid2>[0-9A-Fa-f]{8}) (?P<b10>[0-9A-Fa-f]{2}) (?P<b11>[0-9A-Fa-f]{2})$"
)

RE_INSTR = re.compile(
    r"^\[INSTR-LOOP\] gap ms (?P<gap>\d+) in (?P<section>\S+) "
    r"section_ms (?P<section_ms>\d+) sections_ms (?P<sections_ms>\d+)"
)
RE_HEAP = re.compile(r"^(?P<nodetime>\d{2}:\d{2}:\d{2});\[HEAP\];(?P<a>\d+);(?P<b>\d+);(?P<c>\d+);")
RE_WRONGFW = re.compile(r"^APRS decode - Packet discarded, wrong FW-version <(?P<path>[^>]*)><(?P<ver>[^>]*)>")
RE_NTP_OK = re.compile(r"^\[NTP\];ok;epoch;(?P<epoch>\d+);rtt;(?P<rtt>\d+)")
RE_CET = re.compile(r"\{CET\}(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")


def fmt_dt(dt: datetime) -> str:
    """Host timestamp with millisecond precision, as the raw log writes it."""
    return dt.strftime("%Y-%m-%d %H:%M:%S.") + f"{dt.microsecond // 1000:03d}"


def _semi_fields(rest: str) -> dict[str, str]:
    """Parse a ``key;value;key;value`` style firmware log line body."""
    parts = rest.split(";")
    out: dict[str, str] = {}
    i = 0
    while i + 1 < len(parts):
        out[parts[i]] = parts[i + 1]
        i += 2
    return out


# --------------------------------------------------------------------------
# Airtime
# --------------------------------------------------------------------------


def time_on_air_ms(
    payload_len: int,
    sf: int = 11,
    bw_hz: int = 250_000,
    cr_denom: int = 6,
    preamble: int = 8,
    crc_on: bool = True,
    explicit_header: bool = True,
    ldro: bool = False,
) -> float:
    """Semtech LoRa time-on-air in ms for the MeshCom MOD 8 preset."""
    if payload_len <= 0:
        return 0.0
    tsym_ms = (2**sf) / bw_hz * 1000.0
    de = 1 if ldro else 0
    ih = 0 if explicit_header else 1
    numerator = 8 * payload_len - 4 * sf + 28 + (16 if crc_on else 0) - 20 * ih
    denominator = 4 * (sf - 2 * de)
    n_payload = 8 + max(math.ceil(numerator / denominator) * cr_denom, 0)
    return (preamble + 4.25) * tsym_ms + n_payload * tsym_ms


# --------------------------------------------------------------------------
# Records
# --------------------------------------------------------------------------


@dataclass
class Reception:
    node: str
    host: datetime
    node_time: str
    msg_len: int
    ptype: str
    msg_id: str
    max_hop: int
    server: int
    track: int
    mesh: int
    path: list[str]
    dest: str
    payload: str
    src_hw: int
    mod_country: int
    mod_index: int
    fcs: str
    fw_major: int
    fw_sub: str
    last_hw_raw: int
    raw: str

    @property
    def origin(self) -> str:
        return self.path[0] if self.path else ""

    @property
    def last_hop(self) -> str:
        return self.path[-1] if self.path else ""

    @property
    def hops(self) -> int:
        return max(len(self.path) - 1, 0)

    @property
    def last_hw(self) -> int:
        return self.last_hw_raw & 0x7F

    @property
    def last_hw_flag(self) -> int:
        return (self.last_hw_raw >> 7) & 1

    @property
    def initial_max_hop(self) -> int:
        return self.max_hop + self.hops

    @property
    def fw_label(self) -> str:
        major = self.fw_major
        sub = self.fw_sub if self.fw_sub.strip() else "_"
        return f"{major // 10}.{major % 10}{sub}" if major else f"{major:02d}:{sub}"

    @property
    def is_time_signal(self) -> bool:
        return self.origin in TIME_SIGNAL_CALLS

    @property
    def airtime_ms(self) -> float:
        return time_on_air_ms(self.msg_len)


@dataclass
class NodeLog:
    label: str
    path: Path
    own_call: str | None = None
    first_host: datetime | None = None
    last_host: datetime | None = None
    total_lines: int = 0
    family: Counter = field(default_factory=Counter)
    receptions: list[Reception] = field(default_factory=list)
    undecodable: list[dict[str, Any]] = field(default_factory=list)
    acks: list[dict[str, Any]] = field(default_factory=list)
    instr: list[dict[str, Any]] = field(default_factory=list)
    eth_link: list[dict[str, Any]] = field(default_factory=list)
    eth_stall: list[dict[str, Any]] = field(default_factory=list)
    eth_event: list[dict[str, Any]] = field(default_factory=list)
    ntp_ok: list[dict[str, Any]] = field(default_factory=list)
    ntp_fail: list[str] = field(default_factory=list)
    heap: list[dict[str, Any]] = field(default_factory=list)
    gw_keep: list[datetime] = field(default_factory=list)
    gw_rx: Counter = field(default_factory=Counter)
    gate_inject: list[datetime] = field(default_factory=list)
    rx_udp_paths: list[str] = field(default_factory=list)
    wrong_fw: list[dict[str, Any]] = field(default_factory=list)
    unparsed: list[str] = field(default_factory=list)

    @property
    def duration_s(self) -> float:
        if self.first_host is None or self.last_host is None:
            return 0.0
        return (self.last_host - self.first_host).total_seconds()


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------


def split_body(body: str, ptype: str) -> tuple[list[str], str, str]:
    """Split ``PATH>DEST<type><payload>`` into (path, dest, payload)."""
    if ">" not in body:
        return ([], "", body)
    path_part, remainder = body.split(">", 1)
    path = [c for c in path_part.split(",") if c]
    idx = remainder.find(ptype)
    if idx < 0:
        return (path, remainder, "")
    return (path, remainder[:idx], remainder[idx + 1 :])


def assign_labels(paths: Sequence[Path]) -> list[str]:
    """Short per-log labels, kept unique.

    The capture files are named ``<call>_meshcom_<date>_<time>.log``, so the stem up
    to the first underscore is the natural label.  Two files whose stems share that
    prefix would collapse into one label and silently merge two nodes' statistics, so
    the colliding ones fall back to their full stem.
    """
    short = [path.stem.split("_")[0] for path in paths]
    clashes = {label for label in short if short.count(label) > 1}
    return [path.stem if label in clashes else label for label, path in zip(short, paths)]


def parse_logs(paths: Sequence[Path]) -> list[NodeLog]:
    """Parse every log with collision-free labels, sorted by label."""
    nodes = [parse_log(path, label) for path, label in zip(paths, assign_labels(paths))]
    nodes.sort(key=lambda n: n.label)
    return nodes


def parse_log(path: Path, label: str | None = None) -> NodeLog:
    if label is None:
        label = path.stem.split("_")[0]
    node = NodeLog(label=label, path=path)
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            node.total_lines += 1
            m = RE_PREFIX.match(line)
            if not m:
                node.family["no-prefix"] += 1
                node.unparsed.append(line[:200])
                continue
            host = datetime.strptime(m.group("host"), "%Y-%m-%d %H:%M:%S.%f")
            if node.first_host is None:
                node.first_host = host
            node.last_host = host
            rest = m.group("rest")
            _classify(node, host, rest, line)
    node.own_call = detect_own_call(node)
    return node


def _classify(node: NodeLog, host: datetime, rest: str, raw: str) -> None:
    if "[LOG]" in rest:
        am = RE_APRS.match(rest)
        if am:
            ptype = am.group("type")
            path, dest, payload = split_body(am.group("body"), ptype)
            msg_id = am.group("msgid").upper()
            rec = Reception(
                node=node.label,
                host=host,
                node_time=am.group("nodetime"),
                msg_len=int(am.group("len")),
                ptype=ptype,
                msg_id=msg_id,
                max_hop=int(am.group("hop"), 16),
                server=int(am.group("s")),
                track=int(am.group("t")),
                mesh=int(am.group("m"), 16),
                path=path,
                dest=dest,
                payload=payload,
                src_hw=int(am.group("hw")),
                mod_country=int(am.group("modc"), 16),
                mod_index=int(am.group("modm")),
                fcs=am.group("fcs").upper(),
                fw_major=int(am.group("fwmaj")),
                fw_sub=am.group("fwsub"),
                last_hw_raw=int(am.group("lh"), 16),
                raw=raw,
            )
            # decodeAPRS() leaves the struct at its initAPRS defaults when it bails, so
            # FCS 0000 (and, usually, msg_id 0 with an empty path) marks a decode failure.
            if msg_id == "00000000" or rec.fcs == "0000" or not path:
                node.family["log-undecodable"] += 1
                node.undecodable.append(
                    {"host": fmt_dt(host), "type": ptype, "len": rec.msg_len, "raw": raw}
                )
                return
            node.family["log-aprs"] += 1
            node.receptions.append(rec)
            return
        a7 = RE_ACK7.match(rest)
        if a7:
            node.family["log-ack7"] += 1
            node.acks.append(
                {"host": fmt_dt(host), "size": 7, "type": a7.group("type"), "msg_id": a7.group("msgid").upper()}
            )
            return
        a12 = RE_ACK12.match(rest)
        if a12:
            node.family["log-ack12"] += 1
            node.acks.append(
                {
                    "host": fmt_dt(host),
                    "size": 12,
                    "type": a12.group("type"),
                    "msg_id": a12.group("msgid").upper(),
                    "msg_id2": a12.group("msgid2").upper(),
                }
            )
            return
        node.family["log-unparsed"] += 1
        node.unparsed.append(raw[:250])
        return

    if rest.startswith("[GW];"):
        if rest.startswith("[GW];keep"):
            node.family["gw-keep"] += 1
            node.gw_keep.append(host)
        elif rest.startswith("[GW];rx;"):
            node.family["gw-rx"] += 1
            fields = _semi_fields(rest[len("[GW];rx;") :])
            node.gw_rx[fields.get("type", "?")] += 1
        else:
            node.family["gw-other"] += 1
        return

    if rest.startswith("[ETH];"):
        body = rest[len("[ETH];") :]
        if body.startswith("link;up;"):
            node.family["eth-link"] += 1
            f = _semi_fields(body[len("link;up;") :])
            node.eth_link.append({"host": fmt_dt(host), **f})
        elif body.startswith("stall;"):
            node.family["eth-stall"] += 1
            parts = body.split(";")
            f = _semi_fields(";".join(parts[2:]))
            node.eth_stall.append(
                {"host": fmt_dt(host), "what": parts[1], "ms": int(f.get("ms", 0)), "task": f.get("task", "")}
            )
        elif body.startswith("event;"):
            node.family["eth-event"] += 1
            parts = body.split(";")
            what = parts[1]
            # "event;link;down;ip;1;ms;N" carries a bare state token, "event;dhcp;rc;2;ms;N" does not
            if what == "link":
                state = parts[2]
                f = _semi_fields(";".join(parts[3:]))
            else:
                state = ""
                f = _semi_fields(";".join(parts[2:]))
            node.eth_event.append({"host": fmt_dt(host), "what": what, "state": state, **f})
        else:
            node.family["eth-other"] += 1
        return

    if rest.startswith("[NTP];"):
        nm = RE_NTP_OK.match(rest)
        if nm:
            node.family["ntp-ok"] += 1
            node.ntp_ok.append(
                {"host": fmt_dt(host), "epoch": int(nm.group("epoch")), "rtt": int(nm.group("rtt"))}
            )
        else:
            node.family["ntp-fail"] += 1
            node.ntp_fail.append(f"{fmt_dt(host)} {rest}")
        return

    im = RE_INSTR.match(rest)
    if im:
        node.family["instr-loop"] += 1
        node.instr.append(
            {
                "host": fmt_dt(host),
                "gap_ms": int(im.group("gap")),
                "section": im.group("section"),
                "section_ms": int(im.group("section_ms")),
                "sections_ms": int(im.group("sections_ms")),
            }
        )
        return

    hm = RE_HEAP.match(rest)
    if hm:
        node.family["heap"] += 1
        node.heap.append(
            {
                "host": fmt_dt(host),
                "free": int(hm.group("a")),
                "free2": int(hm.group("b")),
                "largest": int(hm.group("c")),
            }
        )
        return

    wm = RE_WRONGFW.match(rest)
    if wm:
        node.family["wrong-fw"] += 1
        node.wrong_fw.append(
            {"host": fmt_dt(host), "path": wm.group("path"), "ver": wm.group("ver")}
        )
        return

    if rest.startswith("[GATE] Received a LoRa packet to transmit"):
        node.family["gate-inject"] += 1
        node.gate_inject.append(host)
        return
    if rest.startswith("RX-UDP Source-Path:"):
        node.family["rx-udp-path"] += 1
        node.rx_udp_paths.append(rest.split(":", 1)[1])
        return
    if rest.startswith("RX-UDP"):
        node.family["rx-udp-other"] += 1
        return
    if rest.startswith("[UDP_ETH]"):
        node.family["udp-eth"] += 1
        return
    if rest.startswith("UDP Out Buff"):
        node.family["udp-out-buff"] += 1
        return
    if rest.startswith("LOOP GATEWAY"):
        node.family["loop-gateway"] += 1
        return
    if "[BEAT] Heartbeat from server" in rest:
        node.family["beat"] += 1
        return

    node.family["other"] += 1
    node.unparsed.append(raw[:250])


def detect_own_call(node: NodeLog) -> str | None:
    """Own callsign = frequent at path[-2], (almost) never at path[-1]."""
    penult: Counter = Counter()
    last: Counter = Counter()
    for rec in node.receptions:
        if len(rec.path) >= 2:
            penult[rec.path[-2]] += 1
        if rec.path:
            last[rec.path[-1]] += 1
    best: str | None = None
    best_n = 0
    for call, n in penult.items():
        if n <= best_n:
            continue
        if last.get(call, 0) > max(2, 0.05 * n):
            continue
        best, best_n = call, n
    return best


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------


def pct(part: float, whole: float) -> float:
    return round(100.0 * part / whole, 2) if whole else 0.0


def quantile(values: Sequence[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return float(ordered[0])
    pos = q * (len(ordered) - 1)
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return float(ordered[lo])
    return float(ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo))


def stats_block(values: Sequence[float]) -> dict[str, Any]:
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "min": round(min(values), 3),
        "median": round(statistics.median(values), 3),
        "p90": round(quantile(values, 0.90), 3),
        "max": round(max(values), 3),
        "mean": round(statistics.fmean(values), 3),
    }


def hw_name(hw: int) -> str:
    return HARDWARE_TYPES.get(hw, f"unknown({hw})")


def fw_is_cad(major: int, sub: str) -> bool:
    if major > 35:
        return True
    if major < 35:
        return False
    return sub in CAD_FW_LETTERS


def bucket_key(ts: datetime, minutes: int = BUCKET_MINUTES) -> str:
    floor = ts.replace(second=0, microsecond=0)
    floor = floor.replace(minute=(floor.minute // minutes) * minutes)
    return floor.isoformat(sep=" ", timespec="minutes")


# --------------------------------------------------------------------------
# Analyses
# --------------------------------------------------------------------------


def a01_overview(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        recs = n.receptions
        out[n.label] = {
            "file": n.path.name,
            "own_call": n.own_call,
            "first_host": fmt_dt(n.first_host) if n.first_host else None,
            "last_host": fmt_dt(n.last_host) if n.last_host else None,
            "duration_s": round(n.duration_s, 1),
            "duration_hms": str(timedelta(seconds=int(n.duration_s))),
            "total_lines": n.total_lines,
            "line_families": dict(sorted(n.family.items())),
            "receptions": len(recs),
            "receptions_per_hour": round(len(recs) / (n.duration_s / 3600.0), 1) if n.duration_s else 0.0,
            "undecodable_log_lines": len(n.undecodable),
            "ack_frames": len(n.acks),
            "unique_msg_ids": len({r.msg_id for r in recs}),
            "unique_originators": len({r.origin for r in recs}),
            "unique_last_hop_relayers": len({r.last_hop for r in recs}),
            "unique_callsigns_in_paths": len({c for r in recs for c in r.path}),
            "airtime_heard_s": round(sum(r.airtime_ms for r in recs) / 1000.0, 1),
            "airtime_duty_pct": round(
                100.0 * sum(r.airtime_ms for r in recs) / 1000.0 / n.duration_s, 2
            )
            if n.duration_s
            else 0.0,
            "unparsed_lines": len(n.unparsed),
        }
    return out


def a02_types(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        total = len(n.receptions) + len(n.acks) + len(n.undecodable)
        counts: Counter = Counter(r.ptype for r in n.receptions)
        rows: list[dict[str, Any]] = []
        for t in [":", "@", "!"]:
            rows.append(
                {"type": t, "label": TYPE_LABEL[t], "count": counts.get(t, 0), "pct": pct(counts.get(t, 0), total)}
            )
        other = sum(v for k, v in counts.items() if k not in (":", "@", "!"))
        rows.append({"type": "other", "label": "other decoded payload types", "count": other, "pct": pct(other, total)})
        ack7 = sum(1 for a in n.acks if a["size"] == 7)
        ack12 = sum(1 for a in n.acks if a["size"] == 12)
        rows.append({"type": "ACK7", "label": "7-byte ACK frame", "count": ack7, "pct": pct(ack7, total)})
        rows.append({"type": "ACK12", "label": "12-byte ACK frame", "count": ack12, "pct": pct(ack12, total)})
        rows.append(
            {
                "type": "undecodable",
                "label": "decode failure (FCS:0000)",
                "count": len(n.undecodable),
                "pct": pct(len(n.undecodable), total),
            }
        )
        out[n.label] = {"total_log_lines": total, "rows": rows, "pct_sum": round(sum(r["pct"] for r in rows), 2)}
    return out


#: ``max_hop`` is a 4-bit field (src/aprs_functions.cpp:161 decode, :1047 encode).
#: A value at or above this threshold can only come from a wrap-around of the
#: relay decrement, i.e. the frame is circulating past its own hop limit.
MAX_HOP_UNDERFLOW_FROM = 13


def a03_hops(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    long_paths: list[dict[str, Any]] = []
    underflow: list[dict[str, Any]] = []
    seen_long: set[tuple[str, str]] = set()
    for n in nodes:
        taken: Counter = Counter()
        remaining: Counter = Counter()
        initial: Counter = Counter()
        by_type: dict[str, Counter] = defaultdict(Counter)
        n_underflow = 0
        for r in n.receptions:
            taken[r.hops] += 1
            remaining[r.max_hop] += 1
            if r.max_hop < MAX_HOP_UNDERFLOW_FROM:
                initial[r.initial_max_hop] += 1
            else:
                n_underflow += 1
                underflow.append(
                    {
                        "node": n.label,
                        "host": fmt_dt(r.host),
                        "msg_id": r.msg_id,
                        "type": r.ptype,
                        "max_hop": r.max_hop,
                        "hops": r.hops,
                        "origin": r.origin,
                        "path": ",".join(r.path),
                        "text": r.payload[:60],
                    }
                )
            by_type[r.ptype][r.hops] += 1
            if len(r.path) > 4:
                key = (r.msg_id, ",".join(r.path))
                if key not in seen_long:
                    seen_long.add(key)
                    long_paths.append(
                        {
                            "node": n.label,
                            "host": fmt_dt(r.host),
                            "msg_id": r.msg_id,
                            "type": r.ptype,
                            "origin": r.origin,
                            "path": ",".join(r.path),
                            "path_len": len(r.path),
                            "hops": r.hops,
                            "max_hop_left": r.max_hop,
                            "dest": r.dest,
                            "text": r.payload[:70],
                        }
                    )
        total = len(n.receptions)
        out[n.label] = {
            "hops_taken": {str(k): {"count": v, "pct": pct(v, total)} for k, v in sorted(taken.items())},
            "max_hop_remaining": {str(k): {"count": v, "pct": pct(v, total)} for k, v in sorted(remaining.items())},
            "initial_hop_limit": {str(k): {"count": v, "pct": pct(v, total)} for k, v in sorted(initial.items())},
            "hops_taken_by_type": {t: dict(sorted(c.items())) for t, c in sorted(by_type.items())},
            "mean_hops": round(statistics.fmean([r.hops for r in n.receptions]), 3) if n.receptions else 0.0,
            "max_hop_underflow_receptions": n_underflow,
            "path_len_gt4_receptions": sum(1 for r in n.receptions if len(r.path) > 4),
            "path_len_histogram": {
                str(k): v for k, v in sorted(Counter(len(r.path) for r in n.receptions).items())
            },
        }
    out["_long_paths"] = sorted(long_paths, key=lambda d: (d["host"]))
    out["_long_path_summary"] = {
        "distinct_msgid_path_combinations": len(long_paths),
        "by_path_len": {
            str(k): v for k, v in sorted(Counter(d["path_len"] for d in long_paths).items())
        },
        "by_type": dict(Counter(d["type"] for d in long_paths)),
        "top_originators": Counter(d["origin"] for d in long_paths).most_common(15),
    }
    out["_max_hop_underflow"] = {
        "threshold": MAX_HOP_UNDERFLOW_FROM,
        "note": (
            "max_hop is a 4-bit field (src/aprs_functions.cpp:161 / :1047). H >= 13 can only be a "
            "wrap-around of the per-relay decrement, i.e. the frame is being relayed past its hop limit."
        ),
        "receptions": len(underflow),
        "rows": sorted(underflow, key=lambda d: d["host"]),
    }
    return out


def a04_talkers(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        total = len(n.receptions)
        by_origin: dict[str, list[Reception]] = defaultdict(list)
        for r in n.receptions:
            by_origin[r.origin].append(r)
        rows: list[dict[str, Any]] = []
        for call, recs in by_origin.items():
            hw = Counter(r.src_hw for r in recs).most_common(1)[0][0]
            fw = Counter((r.fw_major, r.fw_sub) for r in recs).most_common(1)[0][0]
            rows.append(
                {
                    "origin": call,
                    "receptions": len(recs),
                    "pct_of_receptions": pct(len(recs), total),
                    "unique_msg_ids": len({r.msg_id for r in recs}),
                    "copies_per_msg": round(len(recs) / len({r.msg_id for r in recs}), 2),
                    "hw": hw,
                    "hw_name": hw_name(hw),
                    "fw": f"{fw[0]}:{fw[1]}",
                    "fw_cad": fw_is_cad(fw[0], fw[1]),
                    "airtime_s": round(sum(r.airtime_ms for r in recs) / 1000.0, 1),
                    "time_signal": call in TIME_SIGNAL_CALLS,
                    "types": dict(Counter(r.ptype for r in recs)),
                }
            )
        rows.sort(key=lambda d: -d["receptions"])
        out[n.label] = {
            "total_receptions": total,
            "distinct_originators": len(rows),
            "rows": rows,
            "time_signal": [r for r in rows if r["time_signal"]],
        }
    return out


def a04b_firmware(nodes: list[NodeLog]) -> dict[str, Any]:
    """Node count and traffic share per firmware sub-version letter."""
    out: dict[str, Any] = {}
    for n in nodes:
        total = len(n.receptions)
        total_air = sum(r.airtime_ms for r in n.receptions)
        agg: dict[str, dict[str, Any]] = {}
        for r in n.receptions:
            key = f"{r.fw_major}:{r.fw_sub if r.fw_sub.strip() else '(space)'}"
            e = agg.setdefault(
                key,
                {"fw": key, "cad": fw_is_cad(r.fw_major, r.fw_sub), "receptions": 0, "airtime_ms": 0.0, "nodes": set()},
            )
            e["receptions"] += 1
            e["airtime_ms"] += r.airtime_ms
            e["nodes"].add(r.origin)
        rows: list[dict[str, Any]] = []
        for key, e in agg.items():
            rows.append(
                {
                    "fw": key,
                    "cad": e["cad"],
                    "originator_nodes": len(e["nodes"]),
                    "receptions": e["receptions"],
                    "pct_receptions": pct(e["receptions"], total),
                    "airtime_s": round(e["airtime_ms"] / 1000.0, 1),
                    "pct_airtime": pct(e["airtime_ms"], total_air),
                }
            )
        rows.sort(key=lambda d: -d["receptions"])
        cad_rx = sum(r["receptions"] for r in rows if r["cad"])
        out[n.label] = {
            "rows": rows,
            "pct_sum": round(sum(r["pct_receptions"] for r in rows), 2),
            "cad_receptions_pct": pct(cad_rx, total),
            "precad_receptions_pct": pct(total - cad_rx, total),
            "cad_nodes": len({r.origin for r in n.receptions if fw_is_cad(r.fw_major, r.fw_sub)}),
            "precad_nodes": len({r.origin for r in n.receptions if not fw_is_cad(r.fw_major, r.fw_sub)}),
        }
    # network wide, over the union of the receivers
    all_recs = [r for n in nodes for r in n.receptions]
    fw_by_node: dict[str, Counter] = defaultdict(Counter)
    for r in all_recs:
        fw_by_node[r.origin][f"{r.fw_major}:{r.fw_sub if r.fw_sub.strip() else '(space)'}"] += 1
    node_fw = {call: c.most_common(1)[0][0] for call, c in fw_by_node.items()}
    inv: Counter = Counter(node_fw.values())
    out["_network_node_inventory"] = {
        "nodes_total": len(node_fw),
        "by_fw": dict(sorted(inv.items(), key=lambda kv: -kv[1])),
        "cad_nodes": sum(
            1 for fw in node_fw.values() if fw_is_cad(int(fw.split(":")[0]), fw.split(":")[1])
        ),
        "precad_nodes": sum(
            1 for fw in node_fw.values() if not fw_is_cad(int(fw.split(":")[0]), fw.split(":")[1])
        ),
    }
    return out


def a05_neighbours(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    # firmware per callsign learned from the whole corpus (as originator)
    fw_seen: dict[str, Counter] = defaultdict(Counter)
    hw_seen: dict[str, Counter] = defaultdict(Counter)
    for n in nodes:
        for r in n.receptions:
            fw_seen[r.origin][f"{r.fw_major}:{r.fw_sub}"] += 1
            hw_seen[r.origin][r.src_hw] += 1
    for n in nodes:
        total = len(n.receptions)
        by_last: dict[str, list[Reception]] = defaultdict(list)
        for r in n.receptions:
            by_last[r.last_hop].append(r)
        rows: list[dict[str, Any]] = []
        for call, recs in by_last.items():
            lh = Counter(r.last_hw for r in recs).most_common(1)[0][0]
            fw = fw_seen[call].most_common(1)[0][0] if call in fw_seen else None
            rows.append(
                {
                    "relayer": call,
                    "receptions": len(recs),
                    "pct": pct(len(recs), total),
                    "unique_msg_ids": len({r.msg_id for r in recs}),
                    "lh_hw": lh,
                    "lh_hw_name": hw_name(lh),
                    "lh_flag_set_pct": pct(sum(1 for r in recs if r.last_hw_flag) , len(recs)),
                    "fw_as_originator": fw,
                    "fw_cad": fw_is_cad(int(fw.split(":")[0]), fw.split(":")[1]) if fw else None,
                    "direct_origin_share_pct": pct(sum(1 for r in recs if r.hops == 0), len(recs)),
                    "airtime_s": round(sum(r.airtime_ms for r in recs) / 1000.0, 1),
                }
            )
        rows.sort(key=lambda d: -d["receptions"])
        out[n.label] = {
            "distinct_relayers": len(rows),
            "rows": rows,
            "pct_sum": round(sum(r["pct"] for r in rows), 2),
            "top5_share_pct": round(sum(r["pct"] for r in rows[:5]), 2),
        }
    return out


def a06_redundancy(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        by_id: dict[str, list[Reception]] = defaultdict(list)
        for r in n.receptions:
            by_id[r.msg_id].append(r)
        mult: Counter = Counter(len(v) for v in by_id.values())
        relayers: Counter = Counter(len({r.last_hop for r in v}) for v in by_id.values())
        total = len(n.receptions)
        uniq = len(by_id)
        # airtime split: first copy vs redundant copies, per type
        air_by_type: dict[str, dict[str, float]] = defaultdict(lambda: {"first": 0.0, "redundant": 0.0})
        cnt_by_type: dict[str, dict[str, int]] = defaultdict(lambda: {"first": 0, "redundant": 0})
        for mid, recs in by_id.items():
            ordered = sorted(recs, key=lambda r: r.host)
            t = ordered[0].ptype
            air_by_type[t]["first"] += ordered[0].airtime_ms
            cnt_by_type[t]["first"] += 1
            for r in ordered[1:]:
                air_by_type[t]["redundant"] += r.airtime_ms
                cnt_by_type[t]["redundant"] += 1
        type_rows: list[dict[str, Any]] = []
        for t in sorted(air_by_type):
            a = air_by_type[t]
            c = cnt_by_type[t]
            tot_air = a["first"] + a["redundant"]
            type_rows.append(
                {
                    "type": t,
                    "label": TYPE_LABEL.get(t, t),
                    "unique_msgs": c["first"],
                    "redundant_receptions": c["redundant"],
                    "receptions": c["first"] + c["redundant"],
                    "copies_per_msg": round((c["first"] + c["redundant"]) / c["first"], 2) if c["first"] else 0.0,
                    "airtime_total_s": round(tot_air / 1000.0, 1),
                    "airtime_first_s": round(a["first"] / 1000.0, 1),
                    "airtime_redundant_s": round(a["redundant"] / 1000.0, 1),
                    "pct_airtime_redundant": pct(a["redundant"], tot_air),
                }
            )
        tot_air_all = sum(r.airtime_ms for r in n.receptions)
        red_air_all = sum(tr["airtime_redundant_s"] for tr in type_rows) * 1000.0
        out[n.label] = {
            "receptions": total,
            "unique_msg_ids": uniq,
            "redundant_receptions": total - uniq,
            "pct_redundant_receptions": pct(total - uniq, total),
            "copies_per_msg_mean": round(total / uniq, 3) if uniq else 0.0,
            "multiplicity_histogram": {str(k): v for k, v in sorted(mult.items())},
            "multiplicity_max": max(mult) if mult else 0,
            "distinct_relayers_histogram": {str(k): v for k, v in sorted(relayers.items())},
            "airtime_total_s": round(tot_air_all / 1000.0, 1),
            "airtime_redundant_s": round(red_air_all / 1000.0, 1),
            "pct_airtime_redundant": pct(red_air_all, tot_air_all),
            "by_type": type_rows,
            "top_repeated": [
                {
                    "msg_id": mid,
                    "copies": len(v),
                    "type": v[0].ptype,
                    "origin": v[0].origin,
                    "distinct_relayers": len({r.last_hop for r in v}),
                    "text": v[0].payload[:60],
                }
                for mid, v in sorted(by_id.items(), key=lambda kv: -len(kv[1]))[:15]
            ],
        }
    return out


def a07_cross(receivers: list[NodeLog]) -> dict[str, Any]:
    sets: dict[str, set[str]] = {n.label: {r.msg_id for r in n.receptions} for n in receivers}
    type_of: dict[str, str] = {}
    origin_of: dict[str, str] = {}
    for n in receivers:
        for r in n.receptions:
            type_of.setdefault(r.msg_id, r.ptype)
            origin_of.setdefault(r.msg_id, r.origin)
    union = set().union(*sets.values()) if sets else set()
    labels = [n.label for n in receivers]
    venn: Counter = Counter()
    for mid in union:
        key = "+".join(sorted(l for l in labels if mid in sets[l]))
        venn[key] += 1
    per_node = {}
    for l in labels:
        by_type_union: Counter = Counter(type_of[m] for m in union)
        by_type_node: Counter = Counter(type_of[m] for m in sets[l])
        per_node[l] = {
            "unique_msg_ids": len(sets[l]),
            "pct_of_union": pct(len(sets[l]), len(union)),
            "by_type": {
                t: {
                    "heard": by_type_node.get(t, 0),
                    "union": by_type_union.get(t, 0),
                    "delivery_pct": pct(by_type_node.get(t, 0), by_type_union.get(t, 0)),
                }
                for t in sorted(by_type_union)
            },
            "exclusive": sum(1 for m in sets[l] if all(m not in sets[o] for o in labels if o != l)),
        }
    n_heard: Counter = Counter()
    for mid in union:
        n_heard[sum(1 for l in labels if mid in sets[l])] += 1
    return {
        "receivers": labels,
        "union_msg_ids": len(union),
        "per_node": per_node,
        "venn": dict(sorted(venn.items())),
        "heard_by_n_nodes": {str(k): {"count": v, "pct": pct(v, len(union))} for k, v in sorted(n_heard.items())},
        "all_three": venn.get("+".join(sorted(labels)), 0),
        "exclusive_examples": {
            l: sorted(
                (
                    {"msg_id": m, "type": type_of[m], "origin": origin_of[m]}
                    for m in sets[l]
                    if all(m not in sets[o] for o in labels if o != l)
                ),
                key=lambda d: d["msg_id"],
            )[:10]
            for l in labels
        },
    }


def a08_timing(receivers: list[NodeLog]) -> dict[str, Any]:
    first_at: dict[str, dict[str, datetime]] = defaultdict(dict)
    for n in receivers:
        for r in n.receptions:
            cur = first_at[r.msg_id].get(n.label)
            if cur is None or r.host < cur:
                first_at[r.msg_id][n.label] = r.host
    spreads: list[float] = []
    pairwise: dict[str, list[float]] = defaultdict(list)
    labels = [n.label for n in receivers]
    for mid, m in first_at.items():
        if len(m) >= 2:
            ts = sorted(m.values())
            spreads.append((ts[-1] - ts[0]).total_seconds())
            for i, label_a in enumerate(labels):
                for label_b in labels[i + 1 :]:
                    if label_a in m and label_b in m:
                        pairwise[f"{label_a}->{label_b}"].append(
                            (m[label_b] - m[label_a]).total_seconds()
                        )
    inter: dict[str, list[float]] = {}
    inter_by_type: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    for n in receivers:
        by_id: dict[str, list[Reception]] = defaultdict(list)
        for r in n.receptions:
            by_id[r.msg_id].append(r)
        vals: list[float] = []
        for recs in by_id.values():
            if len(recs) < 2:
                continue
            ordered = sorted(recs, key=lambda r: r.host)
            for prev, cur_rec in zip(ordered, ordered[1:]):
                dt = (cur_rec.host - prev.host).total_seconds()
                vals.append(dt)
                inter_by_type[n.label][prev.ptype].append(dt)
        inter[n.label] = vals
    return {
        "first_reception_spread_s": stats_block(spreads),
        "pairwise_offset_s": {k: stats_block(v) for k, v in sorted(pairwise.items())},
        "inter_arrival_s": {k: stats_block(v) for k, v in inter.items()},
        "inter_arrival_by_type_s": {
            node: {t: stats_block(v) for t, v in sorted(d.items())} for node, d in inter_by_type.items()
        },
        "inter_arrival_all_s": stats_block([v for vals in inter.values() for v in vals]),
    }


def a09_time_signal(nodes: list[NodeLog]) -> dict[str, Any]:
    per_node: dict[str, set[str]] = {}
    delays: dict[str, list[float]] = {}
    slowest: dict[str, list[dict[str, Any]]] = {}
    all_beacons: set[str] = set()
    rx_counts: dict[str, int] = {}
    for n in nodes:
        seen: set[str] = set()
        dl: list[float] = []
        rows: list[dict[str, Any]] = []
        cnt = 0
        for r in n.receptions:
            if not r.is_time_signal:
                continue
            m = RE_CET.search(r.payload)
            if not m:
                continue
            cnt += 1
            ts = m.group("ts")
            seen.add(ts)
            beacon_host = datetime.strptime(ts, "%Y-%m-%d %H:%M:%S") + CET_HOST_OFFSET
            delay = (r.host - beacon_host).total_seconds()
            dl.append(delay)
            rows.append(
                {
                    "beacon_utc": ts,
                    "heard": fmt_dt(r.host),
                    "delay_s": round(delay, 1),
                    "delay_min": round(delay / 60.0, 1),
                    "path": ",".join(r.path),
                }
            )
        if cnt:
            per_node[n.label] = seen
            delays[n.label] = dl
            rx_counts[n.label] = cnt
            all_beacons |= seen
            slowest[n.label] = sorted(rows, key=lambda d: -d["delay_s"])[:10]
    ordered = sorted(all_beacons)
    dts = [datetime.strptime(t, "%Y-%m-%d %H:%M:%S") for t in ordered]
    gaps = [(b - a).total_seconds() for a, b in zip(dts, dts[1:])]
    nominal = statistics.median(gaps) if gaps else 0.0
    missing: list[dict[str, Any]] = []
    if nominal:
        for a, b, g in zip(dts, dts[1:], gaps):
            k = round(g / nominal)
            if k >= 2:
                missing.append(
                    {
                        "after": a.isoformat(sep=" "),
                        "before": b.isoformat(sep=" "),
                        "gap_s": round(g, 1),
                        "missed_beacons": k - 1,
                    }
                )
    per_node_out = {}
    for label, seen in per_node.items():
        missed = sorted(all_beacons - seen)
        per_node_out[label] = {
            "receptions": rx_counts[label],
            "distinct_beacons_heard": len(seen),
            "pct_of_union": pct(len(seen), len(all_beacons)),
            "missed_beacons": missed,
            "missed_count": len(missed),
            "reception_delay_s": stats_block(delays[label]),
            "slowest_receptions": slowest[label],
        }
    return {
        "union_beacons": len(all_beacons),
        "first": ordered[0] if ordered else None,
        "last": ordered[-1] if ordered else None,
        "nominal_interval_s": round(nominal, 1),
        "gap_stats_s": stats_block(gaps),
        "beacons_missed_by_all": missing,
        "beacons_missed_by_all_count": sum(m["missed_beacons"] for m in missing),
        "per_node": per_node_out,
        "note": "beacon identity = the {CET} UTC timestamp in the payload; host clock = UTC+2",
    }


def a10_dedup(nodes: list[NodeLog]) -> dict[str, Any]:
    reappear: list[dict[str, Any]] = []
    turnover: dict[str, Any] = {}
    for n in nodes:
        by_id: dict[str, list[Reception]] = defaultdict(list)
        for r in n.receptions:
            by_id[r.msg_id].append(r)
        for mid, recs in by_id.items():
            ordered = sorted(recs, key=lambda r: r.host)
            for a, b in zip(ordered, ordered[1:]):
                gap = (b.host - a.host).total_seconds()
                if 300.0 < gap < 4 * 3600.0:
                    reappear.append(
                        {
                            "node": n.label,
                            "msg_id": mid,
                            "type": a.ptype,
                            "origin": a.origin,
                            "first": fmt_dt(a.host),
                            "again": fmt_dt(b.host),
                            "gap_s": round(gap, 1),
                            "gap_min": round(gap / 60.0, 1),
                            "path_first": ",".join(a.path),
                            "path_again": ",".join(b.path),
                            "text": a.payload[:60],
                        }
                    )
        # ring turnover proxy: NEW unique msg_ids per rolling window
        firsts = sorted(
            min(r.host for r in recs) for recs in by_id.values()
        )
        win: dict[str, Any] = {}
        for minutes in (10, 20, 40):
            span = timedelta(minutes=minutes)
            counts: list[int] = []
            j = 0
            for i, t in enumerate(firsts):
                while firsts[j] < t - span:
                    j += 1
                counts.append(i - j + 1)
            win[str(minutes)] = {
                "rolling_max_new_msg_ids": max(counts) if counts else 0,
                "median_new_msg_ids": round(statistics.median(counts), 1) if counts else 0.0,
                "p90_new_msg_ids": round(quantile(counts, 0.9), 1) if counts else 0.0,
            }
        turnover[n.label] = win
    reappear.sort(key=lambda d: d["gap_s"], reverse=True)
    # which relay chain carries the late copy? aggregate over the second reception's path
    late_relayer: Counter = Counter()
    late_prefix: Counter = Counter()
    for d in reappear:
        again = d["path_again"].split(",")
        late_relayer[again[-1]] += 1
        if len(again) >= 3:
            late_prefix[",".join(again[1:-1])] += 1
        elif len(again) == 2:
            late_prefix[again[1]] += 1
    return {
        "reappearances": reappear,
        "reappearance_count": len(reappear),
        "reappearance_by_node": dict(Counter(d["node"] for d in reappear)),
        "reappearance_by_type": dict(Counter(d["type"] for d in reappear)),
        "reappearance_by_origin": Counter(d["origin"] for d in reappear).most_common(20),
        "gap_min_stats": stats_block([d["gap_min"] for d in reappear]),
        "late_copy_last_hop": late_relayer.most_common(20),
        "late_copy_relay_chain": late_prefix.most_common(20),
        "ring_turnover": turnover,
        "note": "window 5 min < gap < 4 h between consecutive receptions of the same msg_id at one node",
    }


def a11_traffic(nodes: list[NodeLog]) -> dict[str, Any]:
    per_node: dict[str, Any] = {}
    for n in nodes:
        buckets: dict[str, Counter] = defaultdict(Counter)
        for r in n.receptions:
            buckets[bucket_key(r.host)][r.ptype] += 1
            buckets[bucket_key(r.host)]["all"] += 1
        rows: list[dict[str, Any]] = []
        bucket_totals: list[int] = []
        for k, v in sorted(buckets.items()):
            rows.append(
                {"bucket": k, "all": v["all"], "text": v.get(":", 0), "hey": v.get("@", 0), "pos": v.get("!", 0)}
            )
            bucket_totals.append(v["all"])
        # rolling 60 min busiest window
        hosts = sorted(r.host for r in n.receptions)
        best: tuple[int, datetime | None] = (0, None)
        j = 0
        for i, t in enumerate(hosts):
            while hosts[j] < t - timedelta(hours=1):
                j += 1
            if i - j + 1 > best[0]:
                best = (i - j + 1, hosts[j])
        clock_hour: Counter = Counter(r.host.strftime("%Y-%m-%d %H") for r in n.receptions)
        per_node[n.label] = {
            "buckets_10min": rows,
            "bucket_stats": stats_block(bucket_totals),
            "busiest_rolling_hour": {
                "receptions": best[0],
                "window_start": fmt_dt(best[1]) if best[1] else None,
            },
            "busiest_clock_hour": (
                {"hour": clock_hour.most_common(1)[0][0], "receptions": clock_hour.most_common(1)[0][1]}
                if clock_hour
                else None
            ),
            "by_clock_hour": dict(sorted(clock_hour.items())),
        }
    return per_node


def a12_texts(nodes: list[NodeLog]) -> dict[str, Any]:
    seen: dict[str, dict[str, Any]] = {}
    for n in nodes:
        for r in n.receptions:
            if r.ptype != ":" or r.is_time_signal:
                continue
            e = seen.get(r.msg_id)
            if e is None or r.host < datetime.fromisoformat(e["first_heard"]):
                seen[r.msg_id] = {
                    "msg_id": r.msg_id,
                    "first_heard": fmt_dt(r.host),
                    "first_heard_by": n.label,
                    "origin": r.origin,
                    "dest": r.dest,
                    "hw": hw_name(r.src_hw),
                    "fw": f"{r.fw_major}:{r.fw_sub}",
                    "text": r.payload,
                }
            heard = seen[r.msg_id].setdefault("heard_by", {})
            heard[n.label] = heard.get(n.label, 0) + 1
    rows = sorted(seen.values(), key=lambda d: d["first_heard"])
    telemetry = [r for r in rows if r["dest"] == "100001"]
    chat = [r for r in rows if r["dest"] != "100001"]
    return {
        "unique_text_messages": len(rows),
        "chat_messages": len(chat),
        "telemetry_messages": len(telemetry),
        "rows": rows,
        "dest_histogram": dict(Counter(r["dest"] for r in rows)),
    }


def a13_hey(nodes: list[NodeLog]) -> dict[str, Any]:
    per_node: dict[str, Any] = {}
    grammar: Counter = Counter()
    mismatch: list[dict[str, Any]] = []
    group_field_counts: Counter = Counter()
    mismatch_total = 0
    match_total = 0
    relay_ok: Counter = Counter()
    relay_short: Counter = Counter()
    for n in nodes:
        heys = [r for r in n.receptions if r.ptype == "@"]
        direct = sum(1 for r in heys if r.hops == 0)
        relayed = len(heys) - direct
        hop_hist: Counter = Counter(r.hops for r in heys)
        dest_hist: Counter = Counter(r.dest for r in heys)
        origin_counts: Counter = Counter(r.origin for r in heys)
        by_id: dict[str, list[Reception]] = defaultdict(list)
        for r in heys:
            by_id[r.msg_id].append(r)
        ok = 0
        for r in heys:
            payload = r.payload
            if not payload.startswith("R"):
                grammar["no leading R"] += 1
                continue
            rest = payload[1:]
            elems = [e for e in rest.split(";") if e != ""]
            if not elems:
                grammar["R only (no count)"] += 1
                reports = 0
            else:
                head = elems[0]
                head_fields = head.split(",")
                if len(head_fields) == 1:
                    grammar["R<count>; then one group per relay"] += 1
                    reports = len(elems) - 1
                    for e in elems[1:]:
                        group_field_counts[len(e.split(","))] += 1
                else:
                    # originator emitted a bare "R" (old firmware): the first relay group
                    # sits directly behind it, so elems[0] IS a report, not a count
                    grammar["R (no count) then one group per relay"] += 1
                    reports = len(elems)
                    for e in elems:
                        group_field_counts[len(e.split(","))] += 1
            if reports == r.hops:
                ok += 1
                match_total += 1
                for c in r.path[1:]:
                    relay_ok[c] += 1
            else:
                mismatch_total += 1
                for c in r.path[1:]:
                    relay_short[c] += 1
                if len(mismatch) < 60:
                    mismatch.append(
                        {
                            "node": n.label,
                            "host": fmt_dt(r.host),
                            "msg_id": r.msg_id,
                            "path": ",".join(r.path),
                            "hops": r.hops,
                            "reports": reports,
                            "payload": payload,
                        }
                    )
        total = len(n.receptions)
        per_node[n.label] = {
            "hey_receptions": len(heys),
            "pct_of_all_receptions": pct(len(heys), total),
            "unique_hey_msg_ids": len(by_id),
            "copies_per_hey": round(len(heys) / len(by_id), 2) if by_id else 0.0,
            "direct": direct,
            "relayed": relayed,
            "pct_relayed": pct(relayed, len(heys)),
            "hop_histogram": {str(k): {"count": v, "pct": pct(v, len(heys))} for k, v in sorted(hop_hist.items())},
            "mean_hops": round(statistics.fmean([r.hops for r in heys]), 3) if heys else 0.0,
            "dest_histogram": dict(dest_hist),
            "gateway_hey_pct": pct(dest_hist.get("HG", 0), len(heys)),
            "airtime_s": round(sum(r.airtime_ms for r in heys) / 1000.0, 1),
            "pct_of_airtime": pct(
                sum(r.airtime_ms for r in heys), sum(r.airtime_ms for r in n.receptions)
            ),
            "report_grammar_ok": ok,
            "top_originators": origin_counts.most_common(12),
        }
    suspects: list[dict[str, Any]] = []
    for call in sorted(set(relay_ok) | set(relay_short)):
        short = relay_short.get(call, 0)
        good = relay_ok.get(call, 0)
        if short + good < 20:
            continue
        suspects.append(
            {
                "relay": call,
                "in_short_chains": short,
                "in_complete_chains": good,
                "short_share_pct": pct(short, short + good),
            }
        )
    suspects.sort(key=lambda d: (-d["short_share_pct"], -d["in_short_chains"]))
    return {
        "per_node": per_node,
        "payload_grammar_seen": dict(grammar),
        "relay_group_field_count_histogram": {str(k): v for k, v in sorted(group_field_counts.items())},
        "report_chain_matches_hops": match_total,
        "report_chain_short": mismatch_total,
        "report_chain_match_pct": pct(match_total, match_total + mismatch_total),
        "non_appending_relay_suspects": suspects[:15],
        "group_count_vs_hops_mismatches": mismatch,
        "grammar_note": (
            "payload = 'R' + the originator's own neighbour count, then one ';'-terminated signal-report "
            "group per relay. The current firmware writes a 3-field group <mheard>,<-RSSI>,<SNR> "
            "(src/aprs_functions.cpp:1161 appendHeySignalReport), older builds a 2-field <-RSSI>,<SNR>. "
            "Checked against frames heard DIRECT from their originator (path length 1, payload untouched): "
            "the only two originator shapes are a bare 'R' (no count, older firmware) and 'R<n>;'. So the "
            "first ';'-element is the originator's count only when it carries no comma; otherwise it is "
            "already the first relay's report."
        ),
    }


def a14_flags(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        rows: dict[str, Any] = {}
        for t in sorted({r.ptype for r in n.receptions}):
            sel = [r for r in n.receptions if r.ptype == t]
            rows[t] = {
                "count": len(sel),
                "S": {str(k): v for k, v in sorted(Counter(r.server for r in sel).items())},
                "T": {str(k): v for k, v in sorted(Counter(r.track for r in sel).items())},
                "M": {f"{k:02X}": v for k, v in sorted(Counter(r.mesh for r in sel).items())},
                "S1_pct": pct(sum(1 for r in sel if r.server), len(sel)),
                "T1_pct": pct(sum(1 for r in sel if r.track), len(sel)),
                "M01_pct": pct(sum(1 for r in sel if r.mesh == 1), len(sel)),
            }
        out[n.label] = rows
    return out


def a15_health(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        eth_first = n.eth_link[0] if n.eth_link else {}
        eth_last = n.eth_link[-1] if n.eth_link else {}
        counters = ["downs", "renews", "renew_fail", "resets", "tx_fail", "got_ip_n", "rx_n"]
        trend = {
            k: {"first": eth_first.get(k), "last": eth_last.get(k)} for k in counters if k in eth_last
        }
        gaps = [i["gap_ms"] for i in n.instr]
        gap_hist: Counter = Counter()
        for g in gaps:
            if g < 500:
                gap_hist["<500 ms"] += 1
            elif g < 1000:
                gap_hist["500-999 ms"] += 1
            elif g < 2000:
                gap_hist["1.0-1.9 s"] += 1
            elif g < 5000:
                gap_hist["2.0-4.9 s"] += 1
            else:
                gap_hist[">=5 s"] += 1
        keep_gaps = [
            (b - a).total_seconds() for a, b in zip(n.gw_keep, n.gw_keep[1:])
        ]
        max_ms = [int(e["tx_max_ms"]) for e in n.eth_link if "tx_max_ms" in e]
        rx_max = [int(e["rx_max_ms"]) for e in n.eth_link if "rx_max_ms" in e]
        out[n.label] = {
            "own_call": n.own_call,
            "eth_ip": eth_last.get("ip"),
            "eth_dest": eth_last.get("dest"),
            "eth_counter_trend": trend,
            "eth_link_lines": len(n.eth_link),
            "eth_stalls": dict(Counter(s["what"] for s in n.eth_stall)),
            "eth_stall_ms": stats_block([s["ms"] for s in n.eth_stall]),
            "eth_stall_worst": sorted(n.eth_stall, key=lambda s: -s["ms"])[:5],
            "eth_events": dict(
                Counter(
                    f"{e['what']}:{e['state']}" if e.get("state") else f"{e['what']}:rc{e.get('rc', '?')}"
                    for e in n.eth_event
                )
            ),
            "eth_event_rows": n.eth_event,
            "eth_tx_max_ms": stats_block(max_ms),
            "eth_rx_max_ms": stats_block(rx_max),
            "instr_gaps": len(n.instr),
            "instr_gap_ms": stats_block(gaps),
            "instr_gap_histogram": dict(gap_hist),
            "instr_by_section": dict(Counter(i["section"] for i in n.instr)),
            "instr_worst": sorted(n.instr, key=lambda i: -i["gap_ms"])[:5],
            "ntp_ok": len(n.ntp_ok),
            "ntp_fail": len(n.ntp_fail),
            "ntp_fail_lines": n.ntp_fail,
            "ntp_rtt_ms": stats_block([e["rtt"] for e in n.ntp_ok]),
            "heap": n.heap,
            "gw_keepalives": len(n.gw_keep),
            "gw_keep_interval_s": stats_block(keep_gaps),
            "gw_rx": dict(n.gw_rx),
            "gate_injections": len(n.gate_inject),
            "wrong_fw_discards": len(n.wrong_fw),
            "wrong_fw_rows": n.wrong_fw,
        }
    return out


def a16_own_echo(nodes: list[NodeLog]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for n in nodes:
        call = n.own_call
        if not call:
            out[n.label] = {"own_call": None, "note": "no [LOG] lines - own callsign not derivable"}
            continue
        echoes = [r for r in n.receptions if call in r.path]
        penult = [r for r in echoes if len(r.path) >= 2 and r.path[-2] == call]
        deeper = [r for r in echoes if r not in penult and r.path[-1] != call]
        self_last = [r for r in echoes if r.path[-1] == call]
        by_neigh: Counter = Counter(r.last_hop for r in penult)
        out[n.label] = {
            "own_call": call,
            "receptions_containing_own_call": len(echoes),
            "pct_of_receptions": pct(len(echoes), len(n.receptions)),
            "own_relay_echoed_by_neighbour": len(penult),
            "own_call_deeper_in_path": len(deeper),
            "own_call_as_last_hop": len(self_last),
            "own_call_as_last_hop_lines": [r.raw for r in self_last],
            "echo_neighbours": [
                {"neighbour": c, "echoes": v, "pct": pct(v, len(penult))} for c, v in by_neigh.most_common()
            ],
            "echo_airtime_s": round(sum(r.airtime_ms for r in echoes) / 1000.0, 1),
            "by_type": dict(Counter(r.ptype for r in echoes)),
        }
    return out


#: Neighbour count in a position beacon: ``/A=001099/N33`` -> 33.
RE_POS_NC = re.compile(r"/N(\d{1,3})(?!\d)")

#: Minimum copies a relayer needs before it enters the latency / race tables.
RELAY_MIN_COPIES = 20

#: A reappearance gap at or above this many minutes counts as the "slow" cluster.
LATE_CLUSTER_MIN = 20.0


def spearman(xs: Sequence[float], ys: Sequence[float]) -> float | None:
    """Spearman rank correlation, stdlib only, average ranks for ties."""
    n = len(xs)
    if n < 3 or n != len(ys):
        return None

    def ranks(vals: Sequence[float]) -> list[float]:
        order = sorted(range(len(vals)), key=lambda i: vals[i])
        out = [0.0] * len(vals)
        i = 0
        while i < len(order):
            j = i
            while j + 1 < len(order) and vals[order[j + 1]] == vals[order[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1.0
            for k in range(i, j + 1):
                out[order[k]] = avg
            i = j + 1
        return out

    rx, ry = ranks(xs), ranks(ys)
    mx, my = statistics.fmean(rx), statistics.fmean(ry)
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    den = math.sqrt(sum((a - mx) ** 2 for a in rx) * sum((b - my) ** 2 for b in ry))
    return round(num / den, 4) if den else None


def station_index(nodes: list[NodeLog]) -> dict[str, dict[str, Any]]:
    """Hardware / firmware per callsign.

    ``hw``/``fw`` come from frames the station ORIGINATED (``HW:``/``FW:`` describe the
    originator).  A station that only ever relays never fills those, so ``lh_hw`` is
    taken from the ``LH:`` byte of the frames it handed over -- that byte is rewritten
    by every relay and therefore identifies the relayer's board.
    """
    hw_of: dict[str, Counter] = defaultdict(Counter)
    fw_of: dict[str, Counter] = defaultdict(Counter)
    lh_of: dict[str, Counter] = defaultdict(Counter)
    for n in nodes:
        for r in n.receptions:
            hw_of[r.origin][r.src_hw] += 1
            fw_of[r.origin][f"{r.fw_major}:{r.fw_sub}"] += 1
            lh_of[r.last_hop][r.last_hw] += 1
    calls = set(hw_of) | set(lh_of)
    out: dict[str, dict[str, Any]] = {}
    for call in calls:
        hw = hw_of[call].most_common(1)[0][0] if hw_of.get(call) else None
        lh = lh_of[call].most_common(1)[0][0] if lh_of.get(call) else None
        fw = fw_of[call].most_common(1)[0][0] if fw_of.get(call) else None
        out[call] = {
            "hw": hw_name(hw) if hw is not None else (f"{hw_name(lh)} (from LH byte)" if lh is not None else "unknown"),
            "hw_id": hw if hw is not None else lh,
            "fw": fw or "not observable (never originates)",
            "fw_cad": fw_is_cad(int(fw.split(":")[0]), fw.split(":")[1]) if fw else None,
            "originates": fw is not None,
        }
    return out


def _by_msg_id(node: NodeLog) -> dict[str, list[Reception]]:
    grouped: dict[str, list[Reception]] = defaultdict(list)
    for r in node.receptions:
        grouped[r.msg_id].append(r)
    for v in grouped.values():
        v.sort(key=lambda r: r.host)
    return grouped


def a19_first_relayer(receivers: list[NodeLog]) -> dict[str, Any]:
    """Who delivers the FIRST copy of a message to each node, and by how much."""
    out: dict[str, Any] = {}
    for n in receivers:
        grouped = _by_msg_id(n)
        total_copies: Counter = Counter(r.last_hop for r in n.receptions)
        first_count: Counter = Counter()
        first_hops: Counter = Counter()
        first_direct = 0
        leads: list[float] = []
        leads_by_relayer: dict[str, list[float]] = defaultdict(list)
        by_type_lead: dict[str, list[float]] = defaultdict(list)
        for recs in grouped.values():
            first = recs[0]
            first_count[first.last_hop] += 1
            first_hops[first.hops] += 1
            if first.hops == 0:
                first_direct += 1
            if len(recs) >= 2:
                lead = (recs[1].host - first.host).total_seconds()
                leads.append(lead)
                leads_by_relayer[first.last_hop].append(lead)
                by_type_lead[first.ptype].append(lead)
        rows: list[dict[str, Any]] = []
        for call, cnt in first_count.items():
            lv = leads_by_relayer.get(call, [])
            rows.append(
                {
                    "relayer": call,
                    "first_copies": cnt,
                    "pct_of_msg_ids": pct(cnt, len(grouped)),
                    "total_copies": total_copies[call],
                    "first_copy_rate_pct": pct(cnt, total_copies[call]),
                    "lead_median_s": round(statistics.median(lv), 2) if lv else None,
                    "lead_p90_s": round(quantile(lv, 0.9), 2) if lv else None,
                }
            )
        rows.sort(key=lambda d: -d["first_copies"])
        out[n.label] = {
            "msg_ids": len(grouped),
            "rows": rows,
            "pct_sum": round(sum(r["pct_of_msg_ids"] for r in rows), 2),
            "first_copy_direct": first_direct,
            "first_copy_relayed": len(grouped) - first_direct,
            "first_copy_direct_pct": pct(first_direct, len(grouped)),
            "first_copy_hop_histogram": {
                str(k): {"count": v, "pct": pct(v, len(grouped))} for k, v in sorted(first_hops.items())
            },
            "lead_over_second_copy_s": stats_block(leads),
            "lead_by_type_s": {t: stats_block(v) for t, v in sorted(by_type_lead.items())},
        }
    return out


def a20_relay_latency(receivers: list[NodeLog]) -> dict[str, Any]:
    """How far behind the winner every other copy of the same message arrives."""
    out: dict[str, Any] = {}
    for n in receivers:
        grouped = _by_msg_id(n)
        total_copies: Counter = Counter(r.last_hop for r in n.receptions)
        behind: dict[str, list[float]] = defaultdict(list)
        multihop_first: Counter = Counter()
        for recs in grouped.values():
            t0 = recs[0].host
            if recs[0].hops > 0:
                multihop_first[recs[0].hops] += 1
            for r in recs[1:]:
                behind[r.last_hop].append((r.host - t0).total_seconds())
        rows: list[dict[str, Any]] = []
        for call, vals in behind.items():
            if total_copies[call] < RELAY_MIN_COPIES:
                continue
            rows.append(
                {
                    "relayer": call,
                    "total_copies": total_copies[call],
                    "late_copies": len(vals),
                    "late_share_pct": pct(len(vals), total_copies[call]),
                    "median_s": round(statistics.median(vals), 2),
                    "p90_s": round(quantile(vals, 0.9), 2),
                    "max_s": round(max(vals), 2),
                }
            )
        rows.sort(key=lambda d: -d["median_s"])
        out[n.label] = {
            "min_copies": RELAY_MIN_COPIES,
            "rows": rows,
            "first_copy_multihop_histogram": {str(k): v for k, v in sorted(multihop_first.items())},
            "first_copy_multihop_total": sum(multihop_first.values()),
        }
    return out


def a21_neighbour_count(nodes: list[NodeLog], res: dict[str, Any]) -> dict[str, Any]:
    """Reported neighbour count (NC) per station and whether it predicts the relay race."""
    samples: dict[str, list[int]] = defaultdict(list)
    sources: dict[str, Counter] = defaultdict(Counter)
    station = station_index(nodes)
    seen_ids: dict[str, set[str]] = defaultdict(set)
    for n in nodes:
        for r in n.receptions:
            key = (r.msg_id, r.origin)
            if r.ptype == "!":
                m = RE_POS_NC.search(r.payload)
                if m and key[0] not in seen_ids[r.origin + "!"]:
                    seen_ids[r.origin + "!"].add(key[0])
                    samples[r.origin].append(int(m.group(1)))
                    sources[r.origin]["position /N"] += 1
            elif r.ptype == "@" and r.payload.startswith("R"):
                elems = [e for e in r.payload[1:].split(";") if e != ""]
                # only the "R<count>;" dialect carries the originator's own count
                if elems and "," not in elems[0] and elems[0].isdigit():
                    if key[0] not in seen_ids[r.origin + "@"]:
                        seen_ids[r.origin + "@"].add(key[0])
                        samples[r.origin].append(int(elems[0]))
                        sources[r.origin]["HEY R<n>"] += 1

    # how many of the logging nodes hear the station directly, and its relay volume
    heard_directly: Counter = Counter()
    relay_copies: Counter = Counter()
    for n in nodes:
        direct = {r.last_hop for r in n.receptions}
        for c in direct:
            heard_directly[c] += 1
        for r in n.receptions:
            relay_copies[r.last_hop] += 1

    rows: list[dict[str, Any]] = []
    for call, vals in samples.items():
        st = station.get(call, {})
        rows.append(
            {
                "callsign": call,
                "hw": st.get("hw"),
                "fw": st.get("fw"),
                "fw_cad": st.get("fw_cad"),
                "nc_min": min(vals),
                "nc_median": round(statistics.median(vals), 1),
                "nc_max": max(vals),
                "samples": len(vals),
                "sources": dict(sources[call]),
                "heard_directly_by_n_logs": heard_directly.get(call, 0),
                "copies_as_relayer": relay_copies.get(call, 0),
            }
        )
    rows.sort(key=lambda d: -d["nc_median"])

    # scatter: NC vs first-copy rate vs median delay behind first, per node
    scatter: dict[str, Any] = {}
    corr: dict[str, Any] = {}
    nc_median = {r["callsign"]: r["nc_median"] for r in rows}
    for label in res["19_first_relayer"]:
        first_rows = {r["relayer"]: r for r in res["19_first_relayer"][label]["rows"]}
        lat_rows = {r["relayer"]: r for r in res["20_relay_latency"][label]["rows"]}
        entries: list[dict[str, Any]] = []
        for call, lat in lat_rows.items():
            fr = first_rows.get(call)
            if call not in nc_median:
                continue
            entries.append(
                {
                    "relayer": call,
                    "nc_median": nc_median[call],
                    "total_copies": lat["total_copies"],
                    "first_copies": fr["first_copies"] if fr else 0,
                    "first_copy_rate_pct": fr["first_copy_rate_pct"] if fr else 0.0,
                    "median_delay_behind_first_s": lat["median_s"],
                }
            )
        entries.sort(key=lambda d: -d["nc_median"])
        scatter[label] = entries
        if len(entries) >= 3:
            nc = [e["nc_median"] for e in entries]
            corr[label] = {
                "n": len(entries),
                "spearman_nc_vs_first_copy_rate": spearman(nc, [e["first_copy_rate_pct"] for e in entries]),
                "spearman_nc_vs_median_delay": spearman(nc, [e["median_delay_behind_first_s"] for e in entries]),
                "spearman_nc_vs_total_copies": spearman(nc, [float(e["total_copies"]) for e in entries]),
            }
    pooled: list[tuple[float, float, float, float]] = []
    for label, entries in scatter.items():
        for e in entries:
            pooled.append(
                (
                    e["nc_median"],
                    e["first_copy_rate_pct"],
                    e["median_delay_behind_first_s"],
                    float(e["total_copies"]),
                )
            )
    corr["_pooled"] = (
        {
            "n": len(pooled),
            "spearman_nc_vs_first_copy_rate": spearman([p[0] for p in pooled], [p[1] for p in pooled]),
            "spearman_nc_vs_median_delay": spearman([p[0] for p in pooled], [p[2] for p in pooled]),
            "spearman_nc_vs_total_copies": spearman([p[0] for p in pooled], [p[3] for p in pooled]),
        }
        if len(pooled) >= 3
        else {"n": len(pooled)}
    )

    own: list[dict[str, Any]] = []
    for n in nodes:
        if not n.own_call:
            continue
        vals = samples.get(n.own_call, [])
        own.append(
            {
                "log": n.label,
                "own_call": n.own_call,
                "nc_in_own_beacon_min": min(vals) if vals else None,
                "nc_in_own_beacon_median": round(statistics.median(vals), 1) if vals else None,
                "nc_in_own_beacon_max": max(vals) if vals else None,
                "distinct_direct_neighbours_in_log": len({r.last_hop for r in n.receptions}),
                "ratio_observed_over_reported": (
                    round(len({r.last_hop for r in n.receptions}) / statistics.median(vals), 2)
                    if vals and statistics.median(vals)
                    else None
                ),
            }
        )
    return {
        "note": (
            "NC = the station's own neighbour count. Sources: the '/N<n>' field of its position beacon and "
            "the 'R<n>;' head of its HEY payload (only the current firmware dialect carries it; older "
            "builds emit a bare 'R'). One sample per distinct msg_id."
        ),
        "rows": rows,
        "scatter": scatter,
        "correlation": corr,
        "own_beacon_vs_observed": own,
    }


def a22_wrap_culprits(nodes: list[NodeLog], res: dict[str, Any]) -> dict[str, Any]:
    """Who relays a frame that already had max_hop == 0 and wraps the 4-bit counter."""
    wrap_ids = {r["msg_id"] for r in res["03_hops"]["_max_hop_underflow"]["rows"]}
    per_id: dict[str, list[Reception]] = defaultdict(list)
    for n in nodes:
        for r in n.receptions:
            if r.msg_id in wrap_ids:
                per_id[r.msg_id].append(r)
    culprits: Counter = Counter()
    culprit_ids: dict[str, set[str]] = defaultdict(set)
    station = station_index(nodes)
    timeline: list[dict[str, Any]] = []
    seen_frames: set[tuple[str, str, int]] = set()
    for mid, recs in sorted(per_id.items()):
        recs.sort(key=lambda r: r.host)
        for r in recs:
            key = (mid, ",".join(r.path), r.max_hop)
            timeline.append(
                {
                    "msg_id": mid,
                    "host": fmt_dt(r.host),
                    "node": r.node,
                    "max_hop": r.max_hop,
                    "hops": r.hops,
                    "path": ",".join(r.path),
                    "origin": r.origin,
                    "text": r.payload[:50],
                }
            )
            # H == 15 on a relayed frame: the last hop forwarded a frame whose max_hop was 0
            if r.max_hop == 15 and len(r.path) >= 2 and key not in seen_frames:
                seen_frames.add(key)
                culprits[r.path[-1]] += 1
                culprit_ids[r.path[-1]].add(mid)
    rows: list[dict[str, Any]] = []
    for call, cnt in culprits.most_common():
        st = station.get(call, {})
        rows.append(
            {
                "relayer": call,
                "wrapping_frames": cnt,
                "distinct_msg_ids": len(culprit_ids[call]),
                "hw": st.get("hw", "unknown"),
                "fw": st.get("fw", "not observable (never originates)"),
                "fw_cad": st.get("fw_cad"),
                "originates_own_traffic": st.get("originates", False),
            }
        )
    return {
        "note": (
            "max_hop is 4 bits wide. A reception with H == 15 on a path of length >= 2 means its last hop "
            "relayed a frame that arrived with max_hop == 0. Our firmware guards that unconditionally -- "
            "`if(aprsmsg.max_hop > 0) { ... aprsmsg.max_hop--; ... relay }`, src/lora_functions.cpp:1271-1278 "
            "-- so a station doing this runs a build without that guard. `fw` here is only observable when "
            "the station also originates traffic; the `FW:` field of a relayed frame belongs to the "
            "ORIGINATOR, not the relay. The hardware falls back to the `LH:` byte, which every relay "
            "rewrites with its own board id."
        ),
        "wrapped_msg_ids": sorted(wrap_ids),
        "rows": rows,
        "timeline": timeline,
    }


def a23_late_by_path(nodes: list[NodeLog], res: dict[str, Any]) -> dict[str, Any]:
    """Attribute each 5-35 min late copy to the hop where the delay is introduced."""
    # earliest host time of every (msg_id, exact path) pair over all logging nodes
    first_seen: dict[tuple[str, tuple[str, ...]], datetime] = {}
    first_any: dict[str, datetime] = {}
    station = station_index(nodes)
    for n in nodes:
        for r in n.receptions:
            key = (r.msg_id, tuple(r.path))
            if key not in first_seen or r.host < first_seen[key]:
                first_seen[key] = r.host
            if r.msg_id not in first_any or r.host < first_any[r.msg_id]:
                first_any[r.msg_id] = r.host

    attributed: list[dict[str, Any]] = []
    for d in res["10_dedup"]["reappearances"]:
        mid = d["msg_id"]
        path = d["path_again"].split(",")
        t_late = datetime.fromisoformat(d["again"])
        t0 = first_any.get(mid, t_late)
        # walk the path prefixes: the culprit is the hop with the biggest time jump
        known: list[tuple[int, datetime]] = [(0, t0)]
        for i in range(1, len(path)):
            key = (mid, tuple(path[: i + 1]))
            t = first_seen.get(key)
            if t is not None:
                known.append((i, t))
        if known[-1][0] != len(path) - 1:
            known.append((len(path) - 1, t_late))
        # the delay sits in the segment between the two observed points with the biggest jump;
        # when nothing in between was heard, the whole unobserved chain shares the blame
        best_lo, best_hi = 0, known[-1][0]
        best_gap = -1.0
        for (ia, ta), (ib, tb) in zip(known, known[1:]):
            gap = (tb - ta).total_seconds()
            if gap > best_gap:
                best_gap = gap
                best_lo, best_hi = ia, ib
        chain = path[best_lo + 1 : best_hi + 1] or [path[-1]]
        attributed.append(
            {
                "node": d["node"],
                "msg_id": mid,
                "type": d["type"],
                "origin": d["origin"],
                "gap_min": d["gap_min"],
                "path_again": d["path_again"],
                "culprit_chain": ",".join(chain),
                "culprit_chain_len": len(chain),
                "culprit": chain[-1] if len(chain) == 1 else None,
                "culprit_delay_s": round(best_gap, 1),
                "culprit_delay_min": round(best_gap / 60.0, 1),
                "prefixes_observed": len(known) - 2,
                "cluster": "slow (>= 20 min)" if d["gap_min"] >= LATE_CLUSTER_MIN else "fast (< 20 min)",
                "via_khactzc": "OE3KHA-20,OE3CZC-1" in d["path_again"],
            }
        )

    def describe(call: str) -> dict[str, Any]:
        st = station.get(call, {})
        return {
            "hw": st.get("hw", "unknown"),
            "fw": st.get("fw", "not observable (never originates)"),
            "fw_cad": st.get("fw_cad"),
        }

    def summarise(rows: list[dict[str, Any]]) -> dict[str, Any]:
        agg: dict[str, list[dict[str, Any]]] = defaultdict(list)
        member: Counter = Counter()
        member_delays: dict[str, list[float]] = defaultdict(list)
        for r in rows:
            agg[r["culprit_chain"]].append(r)
            for c in r["culprit_chain"].split(","):
                member[c] += 1
                member_delays[c].append(r["culprit_delay_min"])
        chains: list[dict[str, Any]] = []
        for chain, items in agg.items():
            delays = [r["culprit_delay_min"] for r in items]
            members = chain.split(",")
            chains.append(
                {
                    "culprit_chain": chain,
                    "chain_len": len(members),
                    "unambiguous": len(members) == 1,
                    "count": len(items),
                    "median_delay_min": round(statistics.median(delays), 1),
                    "max_delay_min": round(max(delays), 1),
                    "members": [dict(callsign=c, **describe(c)) for c in members],
                }
            )
        chains.sort(key=lambda d: -d["count"])
        members_out: list[dict[str, Any]] = []
        for call, cnt in member.most_common():
            members_out.append(
                {
                    "callsign": call,
                    "in_delaying_segments": cnt,
                    "median_segment_delay_min": round(statistics.median(member_delays[call]), 1),
                    **describe(call),
                }
            )
        return {"chains": chains, "members": members_out}

    slow = [r for r in attributed if r["cluster"].startswith("slow")]
    fast = [r for r in attributed if r["cluster"].startswith("fast")]
    path_hist = Counter(r["path_again"] for r in slow)
    return {
        "note": (
            "For every late copy the exact path prefixes are looked up across all three logs; the delay is "
            "placed in the segment between the two observed prefixes with the largest time jump. When no "
            "intermediate prefix was heard (prefixes_observed = 0) the whole unobserved chain shares the "
            "blame, so the culprit is a CHAIN, not a single station -- `unambiguous` marks the chains of "
            "length 1. The member table counts how often each station sits inside a delaying segment."
        ),
        "total": len(attributed),
        "slow_cluster": {
            "threshold_min": LATE_CLUSTER_MIN,
            "count": len(slow),
            "gap_min": stats_block([r["gap_min"] for r in slow]),
            "via_OE3KHA-20_OE3CZC-1": sum(1 for r in slow if r["via_khactzc"]),
            "culprits": summarise(slow),
            "top_paths": path_hist.most_common(15),
        },
        "fast_cluster": {
            "count": len(fast),
            "gap_min": stats_block([r["gap_min"] for r in fast]),
            "culprits": summarise(fast),
        },
        "rows": sorted(attributed, key=lambda d: -d["gap_min"]),
    }


# --------------------------------------------------------------------------
# Markdown rendering
# --------------------------------------------------------------------------


def md_cell(value: Any) -> str:
    """Stringify a cell and escape the pipes that would otherwise split the row."""
    if value is None:
        return ""
    return str(value).replace("\n", " ").replace("|", "\\|")


def md_table(headers: Sequence[str], rows: Iterable[Sequence[Any]]) -> str:
    body = [[md_cell(c) for c in r] for r in rows]
    lines = [
        "| " + " | ".join(md_cell(h) for h in headers) + " |",
        "|" + "|".join("---" for _ in headers) + "|",
    ]
    for r in body:
        lines.append("| " + " | ".join(r) + " |")
    return "\n".join(lines)


def render_md(res: dict[str, Any], nodes: list[NodeLog], receivers: list[NodeLog], cmds: list[str]) -> str:
    labels = [n.label for n in nodes]
    rlabels = [n.label for n in receivers]
    o = res["01_overview"]
    p: list[str] = []
    A = p.append

    A("# Berg node log analysis -- OE3 mountain gateways, 2026-09-01/02")
    A("")
    A("Generated by `tools/berglog.py`. All timing uses the host capture clock")
    A("(NTP-synced, CEST = UTC+2). Airtime is the Semtech LoRa time-on-air for the")
    A("MeshCom MOD 8 preset (SF11 / BW 250 kHz / CR 4/6 / preamble 8, explicit header,")
    A("CRC on), applied to the `msg_len` byte count printed on every `[LOG]` line.")
    A("")
    A("## Reproduce")
    A("")
    A("```sh")
    for c in cmds:
        A(c)
    A("```")
    A("")

    # ---- identity ----
    A("## 0. Node identity (derived, not assumed)")
    A("")
    A("A node never receives its own transmission, so its own callsign never occurs as")
    A("the last element of a received path, but occurs one position earlier whenever a")
    A("neighbour relays what this node just relayed. The table is that derivation.")
    A("")
    rows = []
    for n in nodes:
        ov = o[n.label]
        rows.append(
            [
                n.label,
                n.path.name,
                n.own_call or "n/a (no [LOG] lines)",
                ov["receptions"],
                res["16_own_echo"][n.label].get("own_relay_echoed_by_neighbour", 0),
                res["16_own_echo"][n.label].get("own_call_as_last_hop", 0),
            ]
        )
    A(md_table(["log", "file", "derived own call", "receptions", "own relay echoed", "own call as last hop"], rows))
    A("")
    A("Beacon positions seen on air for these calls (`A=` is feet):")
    A("")
    A(md_table(["call", "beacon"], [[k, v] for k, v in res["00_beacons"].items()]))
    A("")

    # ---- anomalies ----
    A("## 0b. Anomalies and caveats found in the data")
    A("")
    for item in res["_anomalies"]:
        A(f"- {item}")
    A("")

    # ---- 1 overview ----
    A("## 1. Overview per log")
    A("")
    rows = []
    for n in nodes:
        ov = o[n.label]
        rows.append(
            [
                n.label,
                ov["own_call"],
                ov["first_host"],
                ov["last_host"],
                ov["duration_hms"],
                ov["total_lines"],
                ov["receptions"],
                ov["receptions_per_hour"],
                ov["unique_msg_ids"],
                ov["unique_originators"],
                ov["unique_last_hop_relayers"],
                ov["airtime_heard_s"],
                ov["airtime_duty_pct"],
            ]
        )
    A(
        md_table(
            [
                "log",
                "call",
                "first",
                "last",
                "duration",
                "lines",
                "rx",
                "rx/h",
                "uniq msg_id",
                "uniq origin",
                "uniq relayer",
                "airtime s",
                "channel busy %",
            ],
            rows,
        )
    )
    A("")
    A("### Line families")
    A("")
    fams = sorted({k for n in nodes for k in o[n.label]["line_families"]})
    rows = [[f] + [o[n.label]["line_families"].get(f, 0) for n in nodes] for f in fams]
    rows.append(["TOTAL"] + [o[n.label]["total_lines"] for n in nodes])
    A(md_table(["family"] + labels, rows))
    A("")

    # ---- 2 types ----
    A("## 2. Message types")
    A("")
    for n in nodes:
        t = res["02_types"][n.label]
        if t["total_log_lines"] == 0:
            A(f"**{n.label}** -- no `[LOG]` lines at all (receive log flag off).")
            A("")
            continue
        A(f"**{n.label}** ({n.own_call}), {t['total_log_lines']} `[LOG]` lines")
        A("")
        A(
            md_table(
                ["type", "meaning", "count", "%"],
                [[r["type"], r["label"], r["count"], f"{r['pct']:.2f}"] for r in t["rows"]],
            )
        )
        A("")
        A(f"Column sum: {t['pct_sum']:.2f} %")
        A("")

    # ---- 3 hops ----
    A("## 3. Hop distribution")
    A("")
    A("`hops taken` = path length - 1. `max_hop left` is the H field. The originator's")
    A("configured hop limit is `H + hops taken`.")
    A("")
    for key, title in (
        ("hops_taken", "Hops taken"),
        ("max_hop_remaining", "max_hop remaining (H field)"),
        ("initial_hop_limit", "Derived originator hop limit (H + hops)"),
    ):
        A(f"### {title}")
        A("")
        allk = sorted({k for n in receivers for k in res["03_hops"][n.label][key]}, key=int)
        rows = []
        for n in receivers:
            d = res["03_hops"][n.label][key]
            rows.append([n.label] + [f"{d[k]['count']} ({d[k]['pct']:.1f}%)" if k in d else "-" for k in allk])
        A(md_table(["log"] + allk, rows))
        A("")
    A("### Mean hops taken")
    A("")
    A(md_table(["log", "mean hops"], [[n.label, res["03_hops"][n.label]["mean_hops"]] for n in receivers]))
    A("")
    A("### Hops taken by payload type")
    A("")
    for n in receivers:
        d = res["03_hops"][n.label]["hops_taken_by_type"]
        allk = sorted({k for c in d.values() for k in c}, key=int)
        rows = [[TYPE_LABEL.get(t, t)] + [d[t].get(k, 0) for k in allk] for t in sorted(d)]
        A(f"**{n.label}**")
        A("")
        A(md_table(["type"] + [str(k) for k in allk], rows))
        A("")
    A("### max_hop underflow")
    A("")
    uf = res["03_hops"]["_max_hop_underflow"]
    A(uf["note"])
    A("")
    A(
        md_table(
            ["log", "receptions with H >= 13"],
            [[n.label, res["03_hops"][n.label]["max_hop_underflow_receptions"]] for n in receivers],
        )
    )
    A("")
    if uf["rows"]:
        A(
            md_table(
                ["host", "node", "msg_id", "t", "H", "hops", "origin", "path", "text"],
                [
                    [r["host"], r["node"], r["msg_id"], r["type"], r["max_hop"], r["hops"], r["origin"], r["path"], r["text"]]
                    for r in uf["rows"]
                ],
            )
        )
    A("")
    lp = res["03_hops"]["_long_paths"]
    ls = res["03_hops"]["_long_path_summary"]
    A(f"### Packets with path length > 4 ({ls['distinct_msgid_path_combinations']} distinct msg_id/path combinations)")
    A("")
    A(
        md_table(
            ["log", "receptions with path length > 4", "% of rx"],
            [
                [
                    n.label,
                    res["03_hops"][n.label]["path_len_gt4_receptions"],
                    f"{pct(res['03_hops'][n.label]['path_len_gt4_receptions'], len(n.receptions)):.2f}",
                ]
                for n in receivers
            ],
        )
    )
    A("")
    A("Path-length histogram of all receptions:")
    A("")
    allk = sorted({k for n in receivers for k in res["03_hops"][n.label]["path_len_histogram"]}, key=int)
    A(
        md_table(
            ["log"] + [f"{k} calls" for k in allk],
            [
                [n.label] + [res["03_hops"][n.label]["path_len_histogram"].get(k, 0) for k in allk]
                for n in receivers
            ],
        )
    )
    A("")
    A(
        f"Distinct long-path combinations by path length: {ls['by_path_len']}; by type: {ls['by_type']}. "
        "Top originators of long paths: "
        + ", ".join(f"{c} ({v})" for c, v in ls["top_originators"])
        + "."
    )
    A("")
    longest = [r for r in lp if r["path_len"] >= 6]
    A(f"Every combination with path length >= 6 ({len(longest)} of {len(lp)}); the full list of all")
    A("path length > 4 combinations is in `berg.json` under `03_hops._long_paths`.")
    A("")
    if longest:
        A(
            md_table(
                ["first seen", "node", "msg_id", "t", "origin", "path", "len", "H left", "dest", "text"],
                [
                    [
                        r["host"],
                        r["node"],
                        r["msg_id"],
                        r["type"],
                        r["origin"],
                        r["path"],
                        r["path_len"],
                        r["max_hop_left"],
                        r["dest"],
                        r["text"],
                    ]
                    for r in longest
                ],
            )
        )
    else:
        A("none")
    A("")

    # ---- 4 talkers ----
    A("## 4. Talkers (originators)")
    A("")
    for n in receivers:
        t = res["04_talkers"][n.label]
        A(f"**{n.label}** -- {t['distinct_originators']} distinct originators, top 25 by receptions")
        A("")
        A(
            md_table(
                ["origin", "rx", "% rx", "uniq msg", "copies/msg", "hw", "fw", "CAD", "airtime s", "types"],
                [
                    [
                        r["origin"] + (" (TIME SIGNAL)" if r["time_signal"] else ""),
                        r["receptions"],
                        f"{r['pct_of_receptions']:.2f}",
                        r["unique_msg_ids"],
                        r["copies_per_msg"],
                        r["hw_name"],
                        r["fw"],
                        "yes" if r["fw_cad"] else "no",
                        r["airtime_s"],
                        " ".join(f"{k}{v}" for k, v in sorted(r["types"].items())),
                    ]
                    for r in t["rows"][:25]
                ],
            )
        )
        A("")
    A("### Time signal separated")
    A("")
    rows = []
    for n in receivers:
        for r in res["04_talkers"][n.label]["time_signal"]:
            rows.append(
                [n.label, r["origin"], r["receptions"], f"{r['pct_of_receptions']:.2f}", r["unique_msg_ids"], r["airtime_s"]]
            )
    A(md_table(["log", "origin", "rx", "% of rx", "uniq msg", "airtime s"], rows))
    A("")

    # ---- 4b firmware ----
    A("## 4b. Firmware distribution")
    A("")
    A("`FW:35:p` = 4.35p, first release with CSMA/CAD. Everything below `p` on major 35")
    A("and every major < 35 transmits without a channel check.")
    A("")
    for n in receivers:
        f = res["04b_firmware"][n.label]
        A(f"**{n.label}** -- CAD receptions {f['cad_receptions_pct']:.2f} %, pre-CAD {f['precad_receptions_pct']:.2f} %")
        A("")
        A(
            md_table(
                ["fw", "CAD", "originator nodes", "rx", "% rx", "airtime s", "% airtime"],
                [
                    [
                        r["fw"],
                        "yes" if r["cad"] else "no",
                        r["originator_nodes"],
                        r["receptions"],
                        f"{r['pct_receptions']:.2f}",
                        r["airtime_s"],
                        f"{r['pct_airtime']:.2f}",
                    ]
                    for r in f["rows"]
                ],
            )
        )
        A("")
        A(f"Column sum: {f['pct_sum']:.2f} %")
        A("")
    inv = res["04b_firmware"]["_network_node_inventory"]
    A(
        f"Network-wide node inventory over all three receivers: {inv['nodes_total']} distinct originators, "
        f"{inv['cad_nodes']} on CAD firmware, {inv['precad_nodes']} pre-CAD."
    )
    A("")
    A(md_table(["fw", "nodes"], [[k, v] for k, v in inv["by_fw"].items()]))
    A("")

    # ---- 5 neighbours ----
    A("## 5. Direct RF neighbours / relay fan-out")
    A("")
    A("`relayer` = last callsign in the path = the station whose transmission this node")
    A("actually demodulated. `lh hw` is decoded from the `LH:` byte (low 7 bits).")
    A("")
    for n in receivers:
        d = res["05_neighbours"][n.label]
        A(f"**{n.label}** -- {d['distinct_relayers']} direct neighbours, top 5 carry {d['top5_share_pct']:.1f} % of receptions")
        A("")
        A(
            md_table(
                ["relayer", "rx", "%", "uniq msg", "lh hw", "fw as originator", "CAD", "direct-origin %", "airtime s"],
                [
                    [
                        r["relayer"],
                        r["receptions"],
                        f"{r['pct']:.2f}",
                        r["unique_msg_ids"],
                        r["lh_hw_name"],
                        r["fw_as_originator"] or "-",
                        "-" if r["fw_cad"] is None else ("yes" if r["fw_cad"] else "no"),
                        f"{r['direct_origin_share_pct']:.1f}",
                        r["airtime_s"],
                    ]
                    for r in d["rows"]
                ],
            )
        )
        A("")
        A(f"Column sum: {d['pct_sum']:.2f} %")
        A("")

    # ---- 6 redundancy ----
    A("## 6. Redundancy")
    A("")
    rows = []
    for n in receivers:
        d = res["06_redundancy"][n.label]
        rows.append(
            [
                n.label,
                d["receptions"],
                d["unique_msg_ids"],
                d["redundant_receptions"],
                f"{d['pct_redundant_receptions']:.2f}",
                d["copies_per_msg_mean"],
                d["multiplicity_max"],
                d["airtime_total_s"],
                d["airtime_redundant_s"],
                f"{d['pct_airtime_redundant']:.2f}",
            ]
        )
    A(
        md_table(
            [
                "log",
                "rx",
                "uniq",
                "redundant rx",
                "% redundant",
                "copies/msg",
                "max copies",
                "airtime s",
                "redundant airtime s",
                "% airtime wasted",
            ],
            rows,
        )
    )
    A("")
    A("### Multiplicity histogram (how often the same msg_id was heard)")
    A("")
    allk = sorted({k for n in receivers for k in res["06_redundancy"][n.label]["multiplicity_histogram"]}, key=int)
    rows = []
    for n in receivers:
        h = res["06_redundancy"][n.label]["multiplicity_histogram"]
        rows.append([n.label] + [h.get(k, 0) for k in allk])
    A(md_table(["log"] + [f"{k}x" for k in allk], rows))
    A("")
    A("### Distinct relayers per msg_id")
    A("")
    allk = sorted({k for n in receivers for k in res["06_redundancy"][n.label]["distinct_relayers_histogram"]}, key=int)
    rows = []
    for n in receivers:
        h = res["06_redundancy"][n.label]["distinct_relayers_histogram"]
        rows.append([n.label] + [h.get(k, 0) for k in allk])
    A(md_table(["log"] + [f"{k} relayer(s)" for k in allk], rows))
    A("")
    A("### Redundant airtime by payload type")
    A("")
    for n in receivers:
        A(f"**{n.label}**")
        A("")
        A(
            md_table(
                ["type", "uniq msgs", "rx", "copies/msg", "airtime s", "first-copy s", "redundant s", "% redundant"],
                [
                    [
                        r["label"],
                        r["unique_msgs"],
                        r["receptions"],
                        r["copies_per_msg"],
                        r["airtime_total_s"],
                        r["airtime_first_s"],
                        r["airtime_redundant_s"],
                        f"{r['pct_airtime_redundant']:.2f}",
                    ]
                    for r in res["06_redundancy"][n.label]["by_type"]
                ],
            )
        )
        A("")
    A("### Most repeated single messages")
    A("")
    for n in receivers:
        A(f"**{n.label}**")
        A("")
        A(
            md_table(
                ["msg_id", "copies", "t", "origin", "distinct relayers", "text"],
                [
                    [r["msg_id"], r["copies"], r["type"], r["origin"], r["distinct_relayers"], r["text"]]
                    for r in res["06_redundancy"][n.label]["top_repeated"]
                ],
            )
        )
        A("")

    # ---- 7 cross ----
    c = res["07_cross_node"]
    A("## 7. Cross-node reception")
    A("")
    A(f"Union over {', '.join(c['receivers'])}: **{c['union_msg_ids']}** distinct msg_ids.")
    A("")
    A(
        md_table(
            ["log", "uniq msg_id", "% of union", "exclusive to this node"],
            [[l, d["unique_msg_ids"], f"{d['pct_of_union']:.2f}", d["exclusive"]] for l, d in c["per_node"].items()],
        )
    )
    A("")
    A("### Delivery rate per payload type (node vs union)")
    A("")
    types = sorted({t for d in c["per_node"].values() for t in d["by_type"]})
    rows = []
    for l, d in c["per_node"].items():
        row = [l]
        for t in types:
            e = d["by_type"].get(t)
            row.append(f"{e['heard']}/{e['union']} ({e['delivery_pct']:.1f}%)" if e else "-")
        rows.append(row)
    A(md_table(["log"] + [TYPE_LABEL.get(t, t) for t in types], rows))
    A("")
    A("### Venn")
    A("")
    A(md_table(["heard by", "msg_ids"], [[k, v] for k, v in c["venn"].items()]))
    A("")
    A(
        md_table(
            ["heard by N nodes", "msg_ids", "%"],
            [[k, v["count"], f"{v['pct']:.2f}"] for k, v in c["heard_by_n_nodes"].items()],
        )
    )
    A("")

    # ---- 8 timing ----
    t = res["08_timing"]
    A("## 8. Timing")
    A("")
    A("### Spread between the first reception at each node (msg_ids heard by >= 2 nodes)")
    A("")
    A(md_table(list(t["first_reception_spread_s"].keys()), [list(t["first_reception_spread_s"].values())]))
    A("")
    A("### Pairwise first-reception offset, seconds (positive = second node later)")
    A("")
    A(
        md_table(
            ["pair", "n", "min", "median", "p90", "max", "mean"],
            [
                [k, v["n"], v.get("min"), v.get("median"), v.get("p90"), v.get("max"), v.get("mean")]
                for k, v in t["pairwise_offset_s"].items()
            ],
        )
    )
    A("")
    A("### Inter-arrival between consecutive copies of the same msg_id at one node, seconds")
    A("")
    A(
        md_table(
            ["log", "n", "min", "median", "p90", "max", "mean"],
            [
                [k, v["n"], v.get("min"), v.get("median"), v.get("p90"), v.get("max"), v.get("mean")]
                for k, v in t["inter_arrival_s"].items()
            ],
        )
    )
    A("")
    A("By payload type:")
    A("")
    rows = []
    for node, d in t["inter_arrival_by_type_s"].items():
        for ty, v in d.items():
            rows.append([node, TYPE_LABEL.get(ty, ty), v["n"], v.get("median"), v.get("p90"), v.get("max")])
    A(md_table(["log", "type", "n", "median", "p90", "max"], rows))
    A("")

    # ---- 9 time signal ----
    ts = res["09_time_signal"]
    A("## 9. Time signal ({CET}) loss")
    A("")
    A(
        f"{ts['union_beacons']} distinct beacons between {ts['first']} and {ts['last']} (UTC payload clock); "
        f"nominal interval {ts['nominal_interval_s']:.0f} s."
    )
    A("")
    A(md_table(list(ts["gap_stats_s"].keys()), [list(ts["gap_stats_s"].values())]))
    A("")
    A("### Beacons no node heard (gap is a multiple of the nominal interval)")
    A("")
    if ts["beacons_missed_by_all"]:
        A(
            md_table(
                ["after", "before", "gap s", "beacons missed"],
                [[m["after"], m["before"], m["gap_s"], m["missed_beacons"]] for m in ts["beacons_missed_by_all"]],
            )
        )
    else:
        A("none")
    A("")
    A(f"Total beacons no node heard: **{ts['beacons_missed_by_all_count']}**")
    A("")
    A("### Per node")
    A("")
    A(
        md_table(
            ["log", "rx", "distinct beacons", "% of union", "missed", "delay median s", "delay p90 s", "delay max s"],
            [
                [
                    l,
                    d["receptions"],
                    d["distinct_beacons_heard"],
                    f"{d['pct_of_union']:.2f}",
                    d["missed_count"],
                    d["reception_delay_s"].get("median"),
                    d["reception_delay_s"].get("p90"),
                    d["reception_delay_s"].get("max"),
                ]
                for l, d in ts["per_node"].items()
            ],
        )
    )
    A("")
    for l, d in ts["per_node"].items():
        if d["missed_beacons"]:
            A(f"- **{l}** missed: {', '.join(d['missed_beacons'])}")
    A("")
    A("### Slowest time-signal copies (reception delay against the beacon's own UTC stamp)")
    A("")
    rows = []
    for l, d in ts["per_node"].items():
        for r in d["slowest_receptions"][:6]:
            rows.append([l, r["beacon_utc"], r["heard"], r["delay_min"], r["path"]])
    A(md_table(["log", "beacon UTC", "heard (host)", "delay min", "path"], rows))
    A("")

    # ---- 10 dedup ----
    dd = res["10_dedup"]
    A("## 10. Dedup-window estimate")
    A("")
    A(dd["note"])
    A("")
    A(f"{dd['reappearance_count']} reappearances: {dd['reappearance_by_node']}; by type: {dd['reappearance_by_type']}")
    A("")
    A("Gap statistics, minutes:")
    A("")
    A(md_table(list(dd["gap_min_stats"].keys()), [list(dd["gap_min_stats"].values())]))
    A("")
    A("Which station handed over the LATE copy (last hop of the second reception):")
    A("")
    A(
        md_table(
            ["last hop of late copy", "count", "% of reappearances"],
            [[c, v, f"{pct(v, dd['reappearance_count']):.2f}"] for c, v in dd["late_copy_last_hop"]],
        )
    )
    A("")
    A("Relay chain between originator and last hop on the late copy:")
    A("")
    A(
        md_table(
            ["relay chain", "count"],
            [[c, v] for c, v in dd["late_copy_relay_chain"]],
        )
    )
    A("")
    A("Originators of reappearing messages:")
    A("")
    A(md_table(["origin", "count"], [[c, v] for c, v in dd["reappearance_by_origin"]]))
    A("")
    top = dd["reappearances"][:60]
    A(f"The {len(top)} largest gaps (full list in `berg.json` under `10_dedup.reappearances`):")
    A("")
    if top:
        A(
            md_table(
                ["node", "msg_id", "t", "origin", "first", "again", "gap min", "path first", "path again", "text"],
                [
                    [
                        r["node"],
                        r["msg_id"],
                        r["type"],
                        r["origin"],
                        r["first"],
                        r["again"],
                        r["gap_min"],
                        r["path_first"],
                        r["path_again"],
                        r["text"],
                    ]
                    for r in top
                ],
            )
        )
    else:
        A("none")
    A("")
    A("### Ring turnover proxy -- NEW msg_ids arriving per rolling window")
    A("")
    rows = []
    for l, w in dd["ring_turnover"].items():
        for minutes in ("10", "20", "40"):
            e = w[minutes]
            rows.append([l, f"{minutes} min", e["rolling_max_new_msg_ids"], e["median_new_msg_ids"], e["p90_new_msg_ids"]])
    A(md_table(["log", "window", "rolling max", "median", "p90"], rows))
    A("")

    # ---- 11 traffic ----
    A("## 11. Traffic over time")
    A("")
    A(
        md_table(
            ["log", "10-min bucket median", "p90", "max", "busiest rolling hour (rx)", "window start", "busiest clock hour"],
            [
                [
                    l,
                    d["bucket_stats"].get("median"),
                    d["bucket_stats"].get("p90"),
                    d["bucket_stats"].get("max"),
                    d["busiest_rolling_hour"]["receptions"],
                    d["busiest_rolling_hour"]["window_start"],
                    f"{d['busiest_clock_hour']['hour']} ({d['busiest_clock_hour']['receptions']})"
                    if d["busiest_clock_hour"]
                    else "-",
                ]
                for l, d in res["11_traffic"].items()
                if d["bucket_stats"]["n"]
            ],
        )
    )
    A("")
    A("### Receptions per clock hour")
    A("")
    hours = sorted({h for d in res["11_traffic"].values() for h in d["by_clock_hour"]})
    rows = []
    for l, d in res["11_traffic"].items():
        if not d["by_clock_hour"]:
            continue
        rows.append([l] + [d["by_clock_hour"].get(h, 0) for h in hours])
    A(md_table(["log"] + [h[-2:] for h in hours], rows))
    A("")
    A("### Receptions per 10-minute bucket, all types")
    A("")
    bkeys = sorted({b["bucket"] for d in res["11_traffic"].values() for b in d["buckets_10min"]})
    idx = {l: {b["bucket"]: b for b in d["buckets_10min"]} for l, d in res["11_traffic"].items()}
    rows = []
    for b in bkeys:
        row = [b]
        for l in rlabels:
            e = idx.get(l, {}).get(b)
            row.append(f"{e['all']} ({e['text']}/{e['hey']}/{e['pos']})" if e else "-")
        rows.append(row)
    A(md_table(["bucket"] + [f"{l} all (txt/hey/pos)" for l in rlabels], rows))
    A("")

    # ---- 12 texts ----
    tx = res["12_texts"]
    A("## 12. Text message log")
    A("")
    A(
        f"{tx['unique_text_messages']} distinct text msg_ids (time signal excluded): "
        f"{tx['chat_messages']} chat/DM, {tx['telemetry_messages']} telemetry to group 100001."
    )
    A("")
    A(
        md_table(
            ["first heard", "by", "msg_id", "origin", "dest", "hw", "fw", "heard by (copies)", "text"],
            [
                [
                    r["first_heard"],
                    r["first_heard_by"],
                    r["msg_id"],
                    r["origin"],
                    r["dest"],
                    r["hw"],
                    r["fw"],
                    " ".join(f"{k}:{v}" for k, v in sorted(r.get("heard_by", {}).items())),
                    r["text"][:180],
                ]
                for r in tx["rows"]
            ],
        )
    )
    A("")

    # ---- 13 hey ----
    h = res["13_hey"]
    A("## 13. HEY ('@') frames")
    A("")
    A(h["grammar_note"])
    A("")
    A(md_table(["payload grammar variant", "receptions"], [[k, v] for k, v in h["payload_grammar_seen"].items()]))
    A("")
    A(
        md_table(
            ["fields per relay group", "count"],
            [[k, v] for k, v in h["relay_group_field_count_histogram"].items()],
        )
    )
    A("")
    A(
        md_table(
            ["log", "HEY rx", "% of all rx", "uniq HEY", "copies/HEY", "direct", "relayed", "% relayed", "mean hops", "airtime s", "% of airtime", "HG share %"],
            [
                [
                    l,
                    d["hey_receptions"],
                    f"{d['pct_of_all_receptions']:.2f}",
                    d["unique_hey_msg_ids"],
                    d["copies_per_hey"],
                    d["direct"],
                    d["relayed"],
                    f"{d['pct_relayed']:.2f}",
                    d["mean_hops"],
                    d["airtime_s"],
                    f"{d['pct_of_airtime']:.2f}",
                    f"{d['gateway_hey_pct']:.1f}",
                ]
                for l, d in h["per_node"].items()
                if d["hey_receptions"]
            ],
        )
    )
    A("")
    A("### HEY hop histogram")
    A("")
    allk = sorted({k for d in h["per_node"].values() for k in d["hop_histogram"]}, key=int)
    rows = []
    for l, d in h["per_node"].items():
        if not d["hey_receptions"]:
            continue
        rows.append([l] + [f"{d['hop_histogram'][k]['count']} ({d['hop_histogram'][k]['pct']:.1f}%)" if k in d["hop_histogram"] else "-" for k in allk])
    A(md_table(["log"] + [f"{k} hops" for k in allk], rows))
    A("")
    A("### Top HEY originators")
    A("")
    for l, d in h["per_node"].items():
        if not d["hey_receptions"]:
            continue
        A(f"**{l}**: " + ", ".join(f"{c} ({v})" for c, v in d["top_originators"]))
    A("")
    A("### Relay-group count vs hops taken")
    A("")
    A(
        f"The report chain length equals the hop count in {h['report_chain_matches_hops']} of "
        f"{h['report_chain_matches_hops'] + h['report_chain_short']} HEY receptions "
        f"({h['report_chain_match_pct']:.2f} %); {h['report_chain_short']} chains are short, i.e. at least one "
        "relay forwarded the frame without appending its own signal report."
    )
    A("")
    A("Relays over-represented in short chains (>= 20 appearances as a relay):")
    A("")
    A(
        md_table(
            ["relay", "in short chains", "in complete chains", "short share %"],
            [
                [s["relay"], s["in_short_chains"], s["in_complete_chains"], f"{s['short_share_pct']:.2f}"]
                for s in h["non_appending_relay_suspects"]
            ],
        )
    )
    A("")
    if h["group_count_vs_hops_mismatches"]:
        A(f"Examples ({len(h['group_count_vs_hops_mismatches'])} shown, capped at 60):")
        A("")
        A(
            md_table(
                ["node", "host", "msg_id", "path", "hops", "groups", "payload"],
                [
                    [m["node"], m["host"], m["msg_id"], m["path"], m["hops"], m["reports"], m["payload"]]
                    for m in h["group_count_vs_hops_mismatches"]
                ],
            )
        )
        A("")

    # ---- 14 flags ----
    A("## 14. S / T / M flag distribution per type")
    A("")
    rows = []
    for l, d in res["14_flags"].items():
        for ty, v in d.items():
            rows.append(
                [
                    l,
                    TYPE_LABEL.get(ty, ty),
                    v["count"],
                    f"{v['S1_pct']:.2f}",
                    f"{v['T1_pct']:.2f}",
                    f"{v['M01_pct']:.2f}",
                    " ".join(f"S{k}={n}" for k, n in v["S"].items()),
                    " ".join(f"M{k}={n}" for k, n in v["M"].items()),
                ]
            )
    A(md_table(["log", "type", "count", "S=1 %", "T=1 %", "M=01 %", "S detail", "M detail"], rows))
    A("")

    # ---- 15 health ----
    A("## 15. Node health (ETH / INSTR-LOOP / NTP / heap / gateway)")
    A("")
    A("### Ethernet counters, first vs last line")
    A("")
    ctrs = ["downs", "renews", "renew_fail", "resets", "tx_fail", "got_ip_n"]
    rows = []
    for l, d in res["15_health"].items():
        tr = d["eth_counter_trend"]
        rows.append(
            [l, d["eth_ip"], d["eth_link_lines"]]
            + [f"{tr[k]['first']} -> {tr[k]['last']}" if k in tr else "-" for k in ctrs]
        )
    A(md_table(["log", "ip", "link lines"] + ctrs, rows))
    A("")
    A("### Ethernet stalls and events")
    A("")
    A(
        md_table(
            ["log", "stalls", "stall ms median", "stall ms max", "events", "tx_max_ms max", "rx_max_ms max"],
            [
                [
                    l,
                    d["eth_stalls"],
                    d["eth_stall_ms"].get("median", "-"),
                    d["eth_stall_ms"].get("max", "-"),
                    d["eth_events"],
                    d["eth_tx_max_ms"].get("max", "-"),
                    d["eth_rx_max_ms"].get("max", "-"),
                ]
                for l, d in res["15_health"].items()
            ],
        )
    )
    A("")
    rows = []
    for l, d in res["15_health"].items():
        for e in d.get("eth_event_rows", []):
            if e["what"] == "link":
                rows.append([l, e["host"], e["state"], e.get("ms", "")])
    if rows:
        A("Ethernet link up/down events:")
        A("")
        A(md_table(["log", "host", "state", "ms since boot"], rows))
        A("")
    A("### Main-loop stalls ([INSTR-LOOP])")
    A("")
    A(
        md_table(
            ["log", "gaps", "median ms", "p90 ms", "max ms", "sections"],
            [
                [l, d["instr_gaps"], d["instr_gap_ms"].get("median", "-"), d["instr_gap_ms"].get("p90", "-"), d["instr_gap_ms"].get("max", "-"), d["instr_by_section"]]
                for l, d in res["15_health"].items()
            ],
        )
    )
    A("")
    A("Gap size histogram:")
    A("")
    hk = ["<500 ms", "500-999 ms", "1.0-1.9 s", "2.0-4.9 s", ">=5 s"]
    A(
        md_table(
            ["log"] + hk,
            [[l] + [d["instr_gap_histogram"].get(k, 0) for k in hk] for l, d in res["15_health"].items()],
        )
    )
    A("")
    A("Worst single stalls:")
    A("")
    rows = []
    for l, d in res["15_health"].items():
        for i in d["instr_worst"]:
            rows.append([l, i["host"], i["gap_ms"], i["section"], i["section_ms"], i["sections_ms"]])
    A(md_table(["log", "host", "gap ms", "section", "section ms", "sections ms"], rows))
    A("")
    A("### NTP, heap, gateway")
    A("")
    A(
        md_table(
            ["log", "NTP ok", "NTP fail", "rtt median ms", "heap samples", "GW keepalives", "keep interval median s", "GW rx", "GATE injections", "wrong-FW discards"],
            [
                [
                    l,
                    d["ntp_ok"],
                    d["ntp_fail"],
                    d["ntp_rtt_ms"].get("median", "-"),
                    len(d["heap"]),
                    d["gw_keepalives"],
                    d["gw_keep_interval_s"].get("median", "-"),
                    d["gw_rx"],
                    d["gate_injections"],
                    d["wrong_fw_discards"],
                ]
                for l, d in res["15_health"].items()
            ],
        )
    )
    A("")
    for l, d in res["15_health"].items():
        if d["ntp_fail_lines"]:
            A(f"- **{l}** NTP failures: {d['ntp_fail_lines']}")
        if d["heap"]:
            A(f"- **{l}** heap: " + "; ".join(f"{e['host']} free={e['free']} largest={e['largest']}" for e in d["heap"]))
    A("")
    A("### Frames discarded for wrong FW version")
    A("")
    rows = []
    for l, d in res["15_health"].items():
        for w in d["wrong_fw_rows"]:
            rows.append([l, w["host"], w["path"], w["ver"]])
    A(md_table(["log", "host", "path", "version byte"], rows) if rows else "none")
    A("")

    # ---- OE3MAG focus ----
    mag = [n for n in nodes if n not in receivers]
    if mag:
        A("### OE3MAG-specific")
        A("")
        for n in mag:
            d = res["15_health"][n.label]
            ov = o[n.label]
            A(f"`{n.path.name}` -- {ov['total_lines']} lines over {ov['duration_hms']}, no `[LOG]` receive lines at all.")
            A("")
            A(
                md_table(
                    ["metric", "value"],
                    [
                        ["ETH ip", d["eth_ip"]],
                        ["ETH dest", d["eth_dest"]],
                        ["ETH link lines", d["eth_link_lines"]],
                        ["ETH counters", json.dumps(d["eth_counter_trend"], sort_keys=True)],
                        ["ETH stalls", json.dumps(d["eth_stalls"])],
                        ["ETH stall ms", json.dumps(d["eth_stall_ms"])],
                        ["ETH events", json.dumps(d["eth_events"])],
                        ["INSTR-LOOP gaps", f"{d['instr_gaps']} ({json.dumps(d['instr_by_section'])})"],
                        ["INSTR gap ms", json.dumps(d["instr_gap_ms"])],
                        ["NTP", f"{d['ntp_ok']} ok / {d['ntp_fail']} fail, rtt median {d['ntp_rtt_ms'].get('median')} ms"],
                        ["Heap", json.dumps(d["heap"])],
                        ["GW keepalives", f"{d['gw_keepalives']}, interval median {d['gw_keep_interval_s'].get('median')} s"],
                        ["GW rx", json.dumps(d["gw_rx"])],
                        ["wrong-FW discards", d["wrong_fw_discards"]],
                    ],
                )
            )
            A("")

    # ---- 16 echo ----
    A("## 16. Own transmissions heard back (echo of own relays)")
    A("")
    A("The `[LOG]` line is printed before the own-transmission check, so a frame whose")
    A("path contains this node's own callsign is our own relay coming back from a")
    A("neighbour that relayed it further.")
    A("")
    A(
        md_table(
            ["log", "own call", "rx with own call in path", "% of rx", "own relay echoed", "own call deeper", "own call as last hop", "echo airtime s", "by type"],
            [
                [
                    l,
                    d.get("own_call"),
                    d.get("receptions_containing_own_call", 0),
                    f"{d.get('pct_of_receptions', 0):.2f}",
                    d.get("own_relay_echoed_by_neighbour", 0),
                    d.get("own_call_deeper_in_path", 0),
                    d.get("own_call_as_last_hop", 0),
                    d.get("echo_airtime_s", 0),
                    d.get("by_type", {}),
                ]
                for l, d in res["16_own_echo"].items()
            ],
        )
    )
    A("")
    for l, d in res["16_own_echo"].items():
        if not d.get("echo_neighbours"):
            continue
        A(f"**{l}** ({d['own_call']}) -- neighbours that echoed our relays")
        A("")
        A(
            md_table(
                ["neighbour", "echoes", "%"],
                [[e["neighbour"], e["echoes"], f"{e['pct']:.2f}"] for e in d["echo_neighbours"]],
            )
        )
        A("")
        if d.get("own_call_as_last_hop_lines"):
            A("Lines where our own callsign is the LAST hop (should be impossible):")
            A("")
            for line in d["own_call_as_last_hop_lines"]:
                A(f"    {line}")
            A("")

    # ---- undecodable ----
    A("## 17. Undecodable `[LOG]` lines")
    A("")
    rows = []
    for n in nodes:
        for u in n.undecodable:
            rows.append([n.label, u["host"], u["type"], u["len"], u["raw"][-90:]])
    A(md_table(["log", "host", "type", "len", "tail of line"], rows) if rows else "none")
    A("")

    # ---- verification ----
    A("## 18. Verification against the raw logs")
    A("")
    A("Each row re-derives a headline number with an independent one-line shell pipeline.")
    A("")
    v = res["_verification"]
    A(
        md_table(
            ["check", "shell pipeline", "shell result", "berglog result", "match"],
            [[r["check"], f"`{r['cmd']}`", r["shell"], r["script"], "OK" if r["match"] else "MISMATCH"] for r in v],
        )
    )
    A("")
    # ---- 19 first relayer ----
    fr = res["19_first_relayer"]
    A("## 19. First relayer -- who wins the relay race")
    A("")
    A("For every msg_id, the copy that arrived FIRST at that node. `first-copy rate` = first")
    A("copies / all copies that relayer delivered, i.e. how often it is the winner when it is")
    A("heard at all.")
    A("")
    A(
        md_table(
            ["log", "msg_ids", "first copy direct (path len 1)", "%", "first copy relayed", "lead over 2nd copy median s", "p90 s"],
            [
                [
                    l,
                    d["msg_ids"],
                    d["first_copy_direct"],
                    f"{d['first_copy_direct_pct']:.2f}",
                    d["first_copy_relayed"],
                    d["lead_over_second_copy_s"].get("median"),
                    d["lead_over_second_copy_s"].get("p90"),
                ]
                for l, d in fr.items()
            ],
        )
    )
    A("")
    A("Hop count of the first copy:")
    A("")
    allk = sorted({k for d in fr.values() for k in d["first_copy_hop_histogram"]}, key=int)
    A(
        md_table(
            ["log"] + [f"{k} hops" for k in allk],
            [
                [l]
                + [
                    f"{d['first_copy_hop_histogram'][k]['count']} ({d['first_copy_hop_histogram'][k]['pct']:.1f}%)"
                    if k in d["first_copy_hop_histogram"]
                    else "-"
                    for k in allk
                ]
                for l, d in fr.items()
            ],
        )
    )
    A("")
    A("Lead of the first copy over the second, by payload type (seconds):")
    A("")
    rows = []
    for l, d in fr.items():
        for t, v in d["lead_by_type_s"].items():
            rows.append([l, TYPE_LABEL.get(t, t), v["n"], v.get("min"), v.get("median"), v.get("p90"), v.get("max")])
    A(md_table(["log", "type", "n", "min", "median", "p90", "max"], rows))
    A("")
    for l, d in fr.items():
        A(f"**{l}** -- first-copy delivery per relayer")
        A("")
        A(
            md_table(
                ["relayer", "first copies", "% of msg_ids", "total copies", "first-copy rate %", "lead median s", "lead p90 s"],
                [
                    [
                        r["relayer"],
                        r["first_copies"],
                        f"{r['pct_of_msg_ids']:.2f}",
                        r["total_copies"],
                        f"{r['first_copy_rate_pct']:.2f}",
                        r["lead_median_s"],
                        r["lead_p90_s"],
                    ]
                    for r in d["rows"]
                ],
            )
        )
        A("")
        A(f"Column sum: {d['pct_sum']:.2f} %")
        A("")

    # ---- 20 relay latency ----
    rl = res["20_relay_latency"]
    A("## 20. Per-neighbour relay latency -- how far behind the winner")
    A("")
    A(
        f"For every msg_id where this relayer's copy is not the first copy at the node, the delay behind "
        f"that first copy. Only relayers with >= {RELAY_MIN_COPIES} copies at the node. Ranked by median."
    )
    A("")
    for l, d in rl.items():
        A(f"**{l}**")
        A("")
        A(
            md_table(
                ["relayer", "total copies", "late copies", "late share %", "median s", "p90 s", "max s"],
                [
                    [
                        r["relayer"],
                        r["total_copies"],
                        r["late_copies"],
                        f"{r['late_share_pct']:.2f}",
                        r["median_s"],
                        r["p90_s"],
                        r["max_s"],
                    ]
                    for r in d["rows"]
                ],
            )
        )
        A("")
    A("Hop count of the first copy when it already arrived multi-hop:")
    A("")
    allk = sorted({k for d in rl.values() for k in d["first_copy_multihop_histogram"]}, key=int)
    A(
        md_table(
            ["log", "multi-hop first copies"] + [f"{k} hops" for k in allk],
            [
                [l, d["first_copy_multihop_total"]]
                + [d["first_copy_multihop_histogram"].get(k, 0) for k in allk]
                for l, d in rl.items()
            ],
        )
    )
    A("")

    # ---- 21 neighbour count ----
    nc = res["21_neighbour_count"]
    A("## 21. Reported neighbour count (NC)")
    A("")
    A(nc["note"])
    A("")
    A(
        md_table(
            ["callsign", "hw", "fw", "CAD", "NC min", "NC median", "NC max", "samples", "sources", "heard directly by n logs", "copies as relayer"],
            [
                [
                    r["callsign"],
                    r["hw"],
                    r["fw"],
                    "-" if r["fw_cad"] is None else ("yes" if r["fw_cad"] else "no"),
                    r["nc_min"],
                    r["nc_median"],
                    r["nc_max"],
                    r["samples"],
                    " ".join(f"{k}:{v}" for k, v in sorted(r["sources"].items())),
                    r["heard_directly_by_n_logs"],
                    r["copies_as_relayer"],
                ]
                for r in nc["rows"]
            ],
        )
    )
    A("")
    A("### NC in the node's own beacon vs direct neighbours actually observed in its log")
    A("")
    A(
        md_table(
            ["log", "own call", "NC min", "NC median", "NC max", "distinct direct neighbours in log", "observed / reported"],
            [
                [
                    r["log"],
                    r["own_call"],
                    r["nc_in_own_beacon_min"],
                    r["nc_in_own_beacon_median"],
                    r["nc_in_own_beacon_max"],
                    r["distinct_direct_neighbours_in_log"],
                    r["ratio_observed_over_reported"],
                ]
                for r in nc["own_beacon_vs_observed"]
            ],
        )
    )
    A("")
    A("### Does NC predict the relay race?")
    A("")
    A(
        md_table(
            ["scope", "n relayers", "Spearman NC vs first-copy rate", "Spearman NC vs median delay behind first", "Spearman NC vs total copies"],
            [
                [
                    k,
                    v.get("n"),
                    v.get("spearman_nc_vs_first_copy_rate"),
                    v.get("spearman_nc_vs_median_delay"),
                    v.get("spearman_nc_vs_total_copies"),
                ]
                for k, v in nc["correlation"].items()
            ],
        )
    )
    A("")
    for l, entries in nc["scatter"].items():
        A(f"**{l}** -- NC vs relay-race performance (relayers with >= {RELAY_MIN_COPIES} copies)")
        A("")
        A(
            md_table(
                ["relayer", "NC median", "total copies", "first copies", "first-copy rate %", "median delay behind first s"],
                [
                    [
                        e["relayer"],
                        e["nc_median"],
                        e["total_copies"],
                        e["first_copies"],
                        f"{e['first_copy_rate_pct']:.2f}",
                        e["median_delay_behind_first_s"],
                    ]
                    for e in entries
                ],
            )
        )
        A("")

    # ---- 22 wrap culprits ----
    wc = res["22_wrap_culprits"]
    A("## 22. Hop-counter wrap culprits")
    A("")
    A(wc["note"])
    A("")
    A(f"Affected msg_ids: {', '.join(wc['wrapped_msg_ids'])}")
    A("")
    A(
        md_table(
            ["relayer that forwarded at H=0", "wrapping frames", "distinct msg_ids", "hw", "fw", "CAD firmware", "originates own traffic"],
            [
                [
                    r["relayer"],
                    r["wrapping_frames"],
                    r["distinct_msg_ids"],
                    r["hw"],
                    r["fw"],
                    "-" if r["fw_cad"] is None else ("yes" if r["fw_cad"] else "no"),
                    "yes" if r.get("originates_own_traffic") else "no",
                ]
                for r in wc["rows"]
            ],
        )
    )
    A("")
    A("Full timeline of every reception of the affected messages:")
    A("")
    A(
        md_table(
            ["host", "log", "msg_id", "H", "hops", "origin", "path", "text"],
            [
                [r["host"], r["node"], r["msg_id"], r["max_hop"], r["hops"], r["origin"], r["path"], r["text"]]
                for r in wc["timeline"]
            ],
        )
    )
    A("")

    # ---- 23 late copies by path ----
    lb = res["23_late_by_path"]
    A("## 23. Late copies (5-35 min) attributed to a hop")
    A("")
    A(lb["note"])
    A("")
    for key, title in (("slow_cluster", "Slow cluster"), ("fast_cluster", "Fast cluster")):
        c = lb[key]
        A(f"### {title} -- {c['count']} of {lb['total']} late copies")
        A("")
        A(md_table(list(c["gap_min"].keys()), [list(c["gap_min"].values())]))
        A("")
        if key == "slow_cluster":
            A(
                f"{c['via_OE3KHA-20_OE3CZC-1']} of the {c['count']} slow copies travel through the pair "
                "`OE3KHA-20,OE3CZC-1`."
            )
            A("")
        A("Delaying segments (a chain longer than 1 means the intermediate hops were never heard,")
        A("so the delay could sit at any member):")
        A("")
        A(
            md_table(
                ["culprit chain", "unambiguous", "count", "median delay min", "max delay min", "member hw / fw"],
                [
                    [
                        r["culprit_chain"],
                        "yes" if r["unambiguous"] else "no",
                        r["count"],
                        r["median_delay_min"],
                        r["max_delay_min"],
                        "; ".join(f"{m['callsign']} {m['hw']} {m['fw']}" for m in r["members"]),
                    ]
                    for r in c["culprits"]["chains"]
                ],
            )
        )
        A("")
        A("Stations appearing inside a delaying segment:")
        A("")
        A(
            md_table(
                ["callsign", "in delaying segments", "median segment delay min", "hw", "fw", "CAD firmware"],
                [
                    [
                        m["callsign"],
                        m["in_delaying_segments"],
                        m["median_segment_delay_min"],
                        m["hw"],
                        m["fw"],
                        "-" if m["fw_cad"] is None else ("yes" if m["fw_cad"] else "no"),
                    ]
                    for m in c["culprits"]["members"]
                ],
            )
        )
        A("")
        if key == "slow_cluster":
            A("Most frequent full paths of the slow copies:")
            A("")
            A(md_table(["path of the late copy", "count"], [[p, v] for p, v in c["top_paths"]]))
            A("")
    A("### The 40 latest copies with their attributed hop")
    A("")
    A(
        md_table(
            ["node", "msg_id", "t", "origin", "gap min", "culprit chain", "culprit delay min", "prefixes observed", "path of late copy"],
            [
                [
                    r["node"],
                    r["msg_id"],
                    r["type"],
                    r["origin"],
                    r["gap_min"],
                    r["culprit_chain"],
                    r["culprit_delay_min"],
                    r["prefixes_observed"],
                    r["path_again"],
                ]
                for r in lb["rows"][:40]
            ],
        )
    )
    A("")

    return "\n".join(p)


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------


def verify(nodes: list[NodeLog], res: dict[str, Any]) -> list[dict[str, Any]]:
    """Re-derive headline numbers with independent one-line pipelines."""
    import subprocess

    checks: list[dict[str, Any]] = []

    def run(cmd: str) -> str:
        try:
            return subprocess.run(
                ["sh", "-c", cmd], capture_output=True, text=True, timeout=120, check=False
            ).stdout.strip()
        except (OSError, subprocess.SubprocessError) as exc:  # pragma: no cover
            return f"ERROR {exc}"

    for n in nodes:
        f = str(n.path)
        # 1) total [LOG] aprs receive lines
        cmd = f"grep -c ' \\[LOG\\] [0-9][0-9][0-9] ' {f!r}"
        shell = run(cmd)
        script = len(n.receptions) + len(n.undecodable)
        checks.append(
            {
                "check": f"{n.label}: [LOG] frame lines",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell == str(script),
            }
        )
        # 2) unique msg_ids
        cmd = (
            f"grep ' \\[LOG\\] ' {f!r} | grep -v 'FCS:0000' | grep -oE 'x[0-9A-F]{{8}}' | sort -u | wc -l"
        )
        shell = run(cmd)
        script = len({r.msg_id for r in n.receptions})
        checks.append(
            {
                "check": f"{n.label}: unique msg_ids",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell.strip() == str(script),
            }
        )
        # 3) {CET} beacon receptions
        cmd = f"grep -c '{{CET}}' {f!r}"
        shell = run(cmd)
        script = sum(1 for r in n.receptions if RE_CET.search(r.payload))
        checks.append(
            {
                "check": f"{n.label}: {{CET}} beacon receptions",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell == str(script),
            }
        )
        # 4) HEY frames
        cmd = f"grep -c ' \\[LOG\\] [0-9][0-9][0-9] @ ' {f!r}"
        shell = run(cmd)
        script = sum(1 for r in n.receptions if r.ptype == "@")
        checks.append(
            {
                "check": f"{n.label}: HEY ('@') receptions",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell == str(script),
            }
        )
        # 5) direct RF neighbours (distinct last path element)
        cmd = (
            f"grep ' \\[LOG\\] ' {f!r} | grep -v 'FCS:0000' | grep -oE ' [A-Z0-9,/-]+>' | tr -d ' >' | "
            f"awk -F, '{{print $NF}}' | sort -u | wc -l"
        )
        shell = run(cmd)
        script = len({r.last_hop for r in n.receptions})
        checks.append(
            {
                "check": f"{n.label}: distinct direct neighbours",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell.strip() == str(script),
            }
        )
        # 6) GW keepalives
        cmd = f"grep -c '\\[GW\\];keep;tx' {f!r}"
        shell = run(cmd)
        script = len(n.gw_keep)
        checks.append(
            {
                "check": f"{n.label}: [GW] keepalives",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell == str(script),
            }
        )
        # 7) redundant receptions = frame lines - undecodable - unique msg_ids
        cmd = (
            f"echo $(( $(grep -c ' \\[LOG\\] [0-9][0-9][0-9] ' {f!r}) "
            f"- $(grep -c 'FCS:0000' {f!r}) "
            f"- $(grep ' \\[LOG\\] ' {f!r} | grep -v 'FCS:0000' | grep -oE 'x[0-9A-F]{{8}}' | sort -u | wc -l) ))"
        )
        shell = run(cmd)
        script = len(n.receptions) - len({r.msg_id for r in n.receptions})
        checks.append(
            {
                "check": f"{n.label}: redundant receptions",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell.strip() == str(script),
            }
        )

        # 8) hop-counter wrap frames: H == 15 (hex 0F)
        cmd = f"grep -cE ' \\[LOG\\] [0-9]{{3}} . x[0-9A-F]{{8}} H0F ' {f!r} || true"
        shell = run(cmd)
        script = sum(1 for r in n.receptions if r.max_hop == 15)
        checks.append(
            {
                "check": f"{n.label}: receptions with max_hop == 15 (wrap)",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell.strip() == str(script),
            }
        )
        # 9) position beacons carrying a /N neighbour-count field
        cmd = f"grep ' \\[LOG\\] [0-9][0-9][0-9] ! ' {f!r} | grep -cE '/N[0-9]' || true"
        shell = run(cmd)
        script = sum(1 for r in n.receptions if r.ptype == "!" and RE_POS_NC.search(r.payload))
        checks.append(
            {
                "check": f"{n.label}: position beacons with /N field",
                "cmd": cmd,
                "shell": shell,
                "script": str(script),
                "match": shell.strip() == str(script),
            }
        )

    receivers = [n for n in nodes if n.receptions]
    files = " ".join(repr(str(n.path)) for n in receivers)
    all_files = " ".join(repr(str(n.path)) for n in nodes)
    # corpus level: union of msg_ids over the receiving nodes
    cmd = f"cat {files} | grep ' \\[LOG\\] ' | grep -v 'FCS:0000' | grep -oE 'x[0-9A-F]{{8}}' | sort -u | wc -l"
    shell = run(cmd)
    script = res["07_cross_node"]["union_msg_ids"]
    checks.append(
        {
            "check": "corpus: union of msg_ids over the 3 receivers",
            "cmd": cmd,
            "shell": shell,
            "script": str(script),
            "match": shell.strip() == str(script),
        }
    )
    # corpus level: distinct {CET} beacons
    cmd = f"grep -h '{{CET}}' {all_files} | grep -oE '\\{{CET\\}}[0-9-]+ [0-9:]+' | sort -u | wc -l"
    shell = run(cmd)
    script = res["09_time_signal"]["union_beacons"]
    checks.append(
        {
            "check": "corpus: distinct {CET} time-signal beacons",
            "cmd": cmd,
            "shell": shell,
            "script": str(script),
            "match": shell.strip() == str(script),
        }
    )
    # corpus level: unique text msg_ids excluding the time signal
    cmd = (
        f"grep -h ' \\[LOG\\] [0-9][0-9][0-9] : ' {all_files} | grep -v '{{CET}}' | grep -v 'FCS:0000' | "
        f"grep -oE 'x[0-9A-F]{{8}}' | sort -u | wc -l"
    )
    shell = run(cmd)
    script = res["12_texts"]["unique_text_messages"]
    checks.append(
        {
            "check": "corpus: unique text msg_ids (time signal excluded)",
            "cmd": cmd,
            "shell": shell,
            "script": str(script),
            "match": shell.strip() == str(script),
        }
    )
    return checks


def collect_anomalies(nodes: list[NodeLog], res: dict[str, Any]) -> list[str]:
    """Facts in the data that contradict expectations; each one is checkable."""
    out: list[str] = []
    for n in nodes:
        label_call = n.label.upper() + "-12"
        if n.own_call and n.own_call != label_call:
            beacon = res["00_beacons"].get(n.own_call, "")
            other = res["00_beacons"].get(label_call, "")
            out.append(
                f"**Identity mismatch**: log file `{n.path.name}` is named `{n.label}` but the node in it is "
                f"**{n.own_call}** (own beacon `{beacon}`), not {label_call} (`{other}`). "
                f"{label_call} appears in this log as a separate station."
            )
        if not n.receptions and n.gw_keep:
            out.append(
                f"**{n.label}** has no `[LOG]` receive lines (receive log flag off) but does send "
                f"{len(n.gw_keep)} `[GW];keep;tx` keepalives and receives {sum(n.gw_rx.values())} server "
                "frames, so it is a server-connected gateway."
            )
        elif not n.receptions:
            out.append(f"**{n.label}** has no `[LOG]` receive lines at all (receive log flag off).")
    for label, d in res["16_own_echo"].items():
        if d.get("own_call_as_last_hop", 0):
            out.append(
                f"**{label}**: {d['own_call_as_last_hop']} reception(s) carry {d['own_call']} as the LAST path "
                "element, i.e. the node logged a frame whose direct sender was itself. A half-duplex LoRa node "
                "cannot hear its own transmission; unexplained. Raw lines are in section 16."
            )
    uf = res["03_hops"]["_max_hop_underflow"]
    if uf["receptions"]:
        origins = Counter(r["origin"] for r in uf["rows"])
        out.append(
            f"**max_hop underflow**: {uf['receptions']} receptions carry H >= {uf['threshold']} although "
            "max_hop is a 4-bit field, so the relay decrement wrapped past zero and the frame kept being "
            f"relayed. Originators: {dict(origins)}."
        )
    n_undec = sum(len(n.undecodable) for n in nodes)
    if n_undec:
        out.append(
            f"**{n_undec} `[LOG]` lines are decode failures** (`FCS:0000`, i.e. `decodeAPRS()` bailed and "
            "left the struct at its defaults -- empty or truncated path/dest/payload); they are excluded "
            "from every message statistic and listed in section 17."
        )
    ts = res["09_time_signal"]
    slow = [
        (label, d["reception_delay_s"].get("max", 0))
        for label, d in ts["per_node"].items()
        if d["reception_delay_s"].get("max", 0) > 600
    ]
    if slow:
        worst = []
        for label, _v in slow:
            row = ts["per_node"][label]["slowest_receptions"][0]
            worst.append(
                f"{label}: beacon {row['beacon_utc']} UTC arrived {row['delay_min']} min late via "
                f"`{row['path']}`"
            )
        med = ", ".join(
            f"{label} {ts['per_node'][label]['reception_delay_s']['median']:.0f} s"
            for label, _ in slow
        )
        out.append(
            "**Time-signal propagation outliers**: median `{CET}` reception delay is "
            + med
            + ", but single copies arrive far later -- "
            + "; ".join(worst)
            + ". A station is flushing a stale transmit queue minutes after the fact."
        )
    sus = [s for s in res["13_hey"]["non_appending_relay_suspects"] if s["short_share_pct"] > 50.0]
    for s in sus:
        out.append(
            f"**{s['relay']} never appends its HEY signal report**: it appears in {s['in_short_chains']} short "
            f"report chains and {s['in_complete_chains']} complete ones ({s['short_share_pct']:.0f} % short)."
        )
    out.append(
        "**Clock caveat**: cross-node timing uses the host capture clock of the collecting machine, so "
        "pairwise offsets include TCP delivery jitter of the console stream, not only RF propagation. The "
        "node clock printed inside each `[LOG]` line has 1 s granularity and lags the host stamp by 0..2 s."
    )
    return out


def collect_beacons(nodes: list[NodeLog]) -> dict[str, str]:
    out: dict[str, str] = {}
    interesting = {n.own_call for n in nodes if n.own_call}
    for n in nodes:
        for r in n.receptions:
            if r.ptype != "!" or r.hops != 0:
                continue
            if r.origin in interesting and r.origin not in out:
                out[r.origin] = f"{r.dest}!{r.payload[:90]}"
    # also add the calls named in the brief if present
    for n in nodes:
        for r in n.receptions:
            if r.ptype == "!" and r.origin in {"OE3XIA-12", "OE3XOC-12", "OE3XWJ-12", "OE3XIR-12", "OE3MAG-12"}:
                out.setdefault(r.origin, f"{r.dest}!{r.payload[:90]}")
    return dict(sorted(out.items()))


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Analyse MeshCom mountain-gateway console logs.")
    ap.add_argument("logs", nargs="+", type=Path, help="log files to analyse")
    ap.add_argument("--out", required=True, type=Path, help="output directory for berg.json / berg.md")
    ap.add_argument("--json-name", default="berg.json")
    ap.add_argument("--md-name", default="berg.md")
    ap.add_argument("--no-verify", action="store_true", help="skip the shell cross-checks")
    args = ap.parse_args(argv)

    args.out.mkdir(parents=True, exist_ok=True)

    nodes = parse_logs(args.logs)
    receivers = [n for n in nodes if n.receptions]

    res: dict[str, Any] = {
        "meta": {
            "generated_by": "tools/berglog.py",
            "logs": [str(p) for p in args.logs],
            "host_clock": "CEST (UTC+2), NTP synced; verified against [NTP];ok;epoch",
            "airtime_model": "SF11 / BW 250 kHz / CR 4/6 / preamble 8 / explicit header / CRC on (MeshCom MOD 8)",
            "hardware_table_source": "src/configuration_global.h //Hardware Types",
            "log_line_source": "src/loop_functions.cpp:3144 printBuffer_aprs, :3151 printBuffer_ack",
        },
        "00_beacons": {},
        "01_overview": a01_overview(nodes),
        "02_types": a02_types(nodes),
        "03_hops": a03_hops(nodes),
        "04_talkers": a04_talkers(nodes),
        "04b_firmware": a04b_firmware(nodes),
        "05_neighbours": a05_neighbours(nodes),
        "06_redundancy": a06_redundancy(nodes),
        "07_cross_node": a07_cross(receivers),
        "08_timing": a08_timing(receivers),
        "09_time_signal": a09_time_signal(nodes),
        "10_dedup": a10_dedup(nodes),
        "11_traffic": a11_traffic(nodes),
        "12_texts": a12_texts(nodes),
        "13_hey": a13_hey(nodes),
        "14_flags": a14_flags(nodes),
        "15_health": a15_health(nodes),
        "16_own_echo": a16_own_echo(nodes),
        "17_hardware_table": {str(k): v for k, v in sorted(HARDWARE_TYPES.items())},
        "19_first_relayer": a19_first_relayer(receivers),
        "20_relay_latency": a20_relay_latency(receivers),
    }
    res["21_neighbour_count"] = a21_neighbour_count(nodes, res)
    res["22_wrap_culprits"] = a22_wrap_culprits(nodes, res)
    res["23_late_by_path"] = a23_late_by_path(nodes, res)
    res["00_beacons"] = collect_beacons(nodes)
    res["_anomalies"] = collect_anomalies(nodes, res)
    res["_verification"] = [] if args.no_verify else verify(nodes, res)

    cmds = [
        "python3 tools/berglog.py \\",
        "    " + " \\\n    ".join(str(p) for p in args.logs) + " \\",
        f"    --out {args.out}",
        "",
        "# table columns are padded for monospace reading:",
        f"npx --yes prettier@3 --write {args.out / args.md_name}",
    ]

    json_path = args.out / args.json_name
    md_path = args.out / args.md_name
    json_path.write_text(json.dumps(res, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
    md_path.write_text(render_md(res, nodes, receivers, cmds), encoding="utf-8")

    # sanity: every percentage table must sum to 100 +- 0.5
    problems: list[str] = []
    for label, d in res["02_types"].items():
        if d["total_log_lines"] and abs(d["pct_sum"] - 100.0) > 0.5:
            problems.append(f"02_types[{label}] sums to {d['pct_sum']}")
    for label, d in res["04b_firmware"].items():
        if label.startswith("_"):
            continue
        if d["rows"] and abs(d["pct_sum"] - 100.0) > 0.5:
            problems.append(f"04b_firmware[{label}] sums to {d['pct_sum']}")
    for label, d in res["05_neighbours"].items():
        if d["rows"] and abs(d["pct_sum"] - 100.0) > 0.5:
            problems.append(f"05_neighbours[{label}] sums to {d['pct_sum']}")
    for label, d in res["19_first_relayer"].items():
        if d["rows"] and abs(d["pct_sum"] - 100.0) > 0.5:
            problems.append(f"19_first_relayer[{label}] sums to {d['pct_sum']}")
        got = sum(r["first_copies"] for r in d["rows"])
        if got != d["msg_ids"]:
            problems.append(f"19_first_relayer[{label}] first copies {got} != msg_ids {d['msg_ids']}")
    for label, d in res["06_redundancy"].items():
        if label not in res["20_relay_latency"]:
            continue
        late = sum(r["late_copies"] for r in res["20_relay_latency"][label]["rows"])
        if late > d["redundant_receptions"]:
            problems.append(
                f"20_relay_latency[{label}] late copies {late} exceed redundant receptions "
                f"{d['redundant_receptions']}"
            )
    for p in problems:
        print(f"WARNING: percentage table does not sum to 100: {p}", file=sys.stderr)

    bad = [c for c in res["_verification"] if not c["match"]]
    for c in bad:
        print(f"WARNING: verification mismatch: {c['check']} shell={c['shell']} script={c['script']}", file=sys.stderr)

    print(f"wrote {json_path}")
    print(f"wrote {md_path}")
    return 1 if (problems or bad) else 0


if __name__ == "__main__":
    raise SystemExit(main())
