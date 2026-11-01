#!/usr/bin/env python3
"""Per-node baseline for the 2026-10-31/11-01 24-h MeshCom captures.

Usage (via run.py): node_baseline.py [node ...]; reads <norm>/<node>.log + <master>/master.tsv, distances.tsv; writes <out>/results.json

One streaming pass per log (no whole-file reads).  Counts events, not lines:
  * RX statistics come from `[LOG]` reception lines, FCS:0000 excluded.  They are printed
    BEFORE dedup / own-TX filtering, so they are receptions, not accepted packets.
  * RSSI/SNR is attributed to the LAST path element only (direct neighbour).
OE3 logs carry a logger wall-clock prefix.  db0ais.log has none: its wall clock is derived from
the node millis() counter (t=, ts=, ms;) plus BOOT0 (PuTTY header 17:01:12 local, +-5 s).
"""
from __future__ import annotations

import collections
import datetime as dt
import json
import math
import re
import statistics
import sys
from pathlib import Path
from typing import Any

from nodeset import ALL as NODES, MASTER, NORM, OUT, STANDALONE, STANDALONE_START

DB0AIS_BOOT0 = dt.datetime.fromisoformat(STANDALONE_START)  # standalone logs: logger/PC clock (CET) at node ms==0
TIME_SERVERS = {"OE1XAR-33", "OE1XAR-62", "OE1XAR-63"}

HW_NAMES = {
    1: "TTGO LoRa32 V2", 2: "TTGO LoRa32 V1", 3: "TTGO LoRa32 V2.1 1.6", 4: "T-Beam",
    5: "T-Beam 1268", 6: "T-Beam 0.7", 7: "T-Echo", 8: "T-Deck", 9: "RAK4631",
    10: "Heltec V2.1", 11: "Heltec V1", 12: "T-Beam V1.2 (AXP2101)", 39: "EBYTE E22",
    40: "T5 E-Paper", 41: "Heltec Tracker", 42: "Heltec Wireless Stick V3",
    43: "Heltec WiFi LoRa 32 V3", 44: "Heltec E290", 45: "T-Beam 1262", 46: "T-Deck Plus",
    47: "T-Beam Supreme", 48: "ESP32-S3 EBYTE E22", 49: "T-LoRa Pager", 50: "T-Deck Pro",
    51: "T-Beam 1W", 52: "Heltec V4", 53: "T-ETH Elite 1262", 54: "Heltec T114",
    55: "T3-S3 V1.3", 56: "T-Connect Pro", 57: "Heltec Wireless Paper", 58: "Heltec E213",
    59: "ESP32 LoRaPRS E22", 60: "ESP32 LoRaPRS RA01", 61: "T-Watch S3",
}

RE_PREFIX = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  ")
RE_RX = re.compile(
    r"^(\d\d):(\d\d):(\d\d) \[LOG\] (\d{3}) (\S) x([0-9A-Fa-f]{8}) H([0-9A-Fa-f]{2}) "
    r"S(\d) T(\d) M(\d+) (.*?) HW:(\w+) MOD:(\S+) FCS:(\w{4}) FW:(\d+):(\S+) LH:(\w+)"
    r"(?: RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w) OWN:(\S) t=(\d+))?"
)
RE_ACK = re.compile(
    r"^(\d\d):(\d\d):(\d\d) \[LOG\] (007|012) A x([0-9A-Fa-f]{8}) H([0-9A-Fa-f]{2}) .*?"
    r"RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w) OWN:(\S) t=(\d+)"
)
RE_TX = re.compile(
    r"^(\d\d):(\d\d):(\d\d) \[LOG\] TX x([0-9A-Fa-f]{8}) (\S) H([0-9A-Fa-f]{2}) prio=(\d) "
    r"src=(\w) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+) t=(\d+)"
)
RE_STAT = re.compile(r"^(\d\d):(\d\d):(\d\d) \[LOG\] STAT (.*)$")
RE_RLY = re.compile(r"\[LOG\] RLY x\w+ (\S) H\w+ q=(\w+) prio=(\d)")
RE_ERRL = re.compile(r"^(\d\d):(\d\d):(\d\d) \[LOG\] ERR rssi=(-?\d+) snr=(-?\d+) len=(\d+) ferr=(-?\d+) t=(\d+)")
RE_RXERR = re.compile(r"\[MC-DBG\] RX_ERROR rssi=(-?\d+) snr=(-?\d+) ts=(\d+)")
RE_CRCERR = re.compile(r"\[MC-DBG\] CRC_ERROR rssi=(-?\d+) snr=(-?\d+)")
RE_STALE = re.compile(r"\[MC-DBG\] RX_IRQ_STALE rssi=(-?\d+)")
RE_CU = re.compile(r"\[MC-DBG\] CHANNEL_UTIL rx=(\d+)ms tx=(\d+)ms util=(\d+)%")
RE_MARK = re.compile(r"\[MC-DBG\] ([A-Z][A-Z0-9_]+)")
RE_GATE = re.compile(r"TX_GATE_ENTER qlen=(\d+) cad_attempt=(\d+)")
RE_CADFREE = re.compile(r"CAD_FREE attempt=(\d+)")
RE_ONRX = re.compile(r"ONRXDONE_TIME ms=(\d+)(?!\w)")
RE_RINGST = re.compile(r"RING_STATUS queued=(\d+) pending=(\d+) retrying=(\d+) done=(\d+)")
RE_RINGW = re.compile(r"RING_WRITE slot=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=(\w+) queued=(\d+)/(\d+) src=(\w+)")
RE_TXREAD = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=(\w+) status=(\w+) len=(\d+) msg_id=(\w+) retry=(\d+)")
RE_DROP = re.compile(r"(RING_DROP_STALE|RING_DROP_PRIO|RING_DROP_NEW|RING_DROP|RING_OVERFLOW)\b(.*)")
RE_SLOT = re.compile(r"RX_DEDUP_ADD msg_id=\w+ srv=\d+ slot=(\d+)/(\d+)")
RE_KEEPID = re.compile(r"\[KEEP\]\.\.\.KEEP([0-9A-F]{8})(.+?)(\d\.\d{2}[a-z])(.*)$")
RE_GWMS = re.compile(r";ms;(\d+)")
RE_ETHSUM = re.compile(
    r"\[ETH\];link;(\w+);link;(\d);link_age_s;(\d+);ip;([\d.]+);dest;([\d.]+);hb_age_s;(\d+);"
    r"got_ip_n;(\d+);downs;(\d+);renews;(\d+);renew_fail;(\d+);resets;(\d+);rx_n;(\d+);"
    r"rx_max_ms;(\d+);tx_fail;(\d+);tx_max_ms;(\d+);ms;(\d+)"
)
RE_ETHSTALL = re.compile(r"\[ETH\];stall;(\w+);ms;(\d+);task;(\w+)")
RE_ETHEV = re.compile(r"\[ETH\];event;([\w]+);?(.*)")
RE_UDPNR = re.compile(r"\[UDP\] Server not responding for (\d+)s - ETH (\w+)")
RE_NTP = re.compile(r"\[NTP\];(ok|timeout);(.*)")
RE_CRASH = re.compile(r"panic|abort\(|watchdog|\bwdt\b|backtrace|guru meditation|assert failed|stack overflow", re.I)
RE_BOOT = re.compile(r"START CLIENT|\[BOOT\];ready|RESETREAS|rst:0x|ets Jun|ets Jul")
RE_NEW = re.compile(r"^\d\d:\d\d:\d\d NEW-(\w+) (\d+) (\S) x\w+ H\w+ S\d T\d M\d+ (\S+?)[,>].*? HW:(\w+) MOD:(\S+) FCS:\w+ FW:(\d+):(\S)")
RE_TXFRAME = re.compile(r"\[MC-TEST\] TX_FRAME len=(\d+) hex=([0-9A-Fa-f]+)")


def toa_ms(pl: int, sf: int = 11, bw: float = 250e3, cr: int = 2, pre: int = 8) -> float:
    """LoRa time on air (explicit header, CRC on); SF11/BW250/CR4:6 default (DE=0)."""
    ts = (2 ** sf) / bw * 1000.0
    de = 1 if ts > 16.0 else 0
    num = 8 * pl - 4 * sf + 28 + 16
    nsym = 8 + max(math.ceil(num / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (pre + 4.25 + nsym) * ts


def pct(vals: list[float], p: float) -> float | None:
    if not vals:
        return None
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def summ(vals: list[float], nd: int = 1) -> dict[str, Any]:
    if not vals:
        return {"n": 0}
    return {
        "n": len(vals), "min": round(min(vals), nd), "p5": round(pct(vals, 5), nd),
        "median": round(pct(vals, 50), nd), "mean": round(statistics.fmean(vals), nd),
        "p95": round(pct(vals, 95), nd), "max": round(max(vals), nd),
    }


def fw_class(major: int, letter: str) -> str:
    """pre_cad: 4.35a..o (and any 4.3x below 4.35); post: 4.35p..z, 4.40*; server: major 0."""
    if major == 0:
        return "server"
    if major < 35:
        return "pre_cad"
    if major == 35:
        if letter and "a" <= letter <= "o":
            return "pre_cad"
        if letter and "p" <= letter <= "z":
            return "post_cad"
        return "unknown_4.35"  # letter '#': sub-version not transmitted, cannot be classified
    return "post_cad"


def central_delta(node_s: int, log_s: int) -> int:
    d = (node_s - log_s) % 86400
    return d - 86400 if d > 43200 else d


def load_master(mdir: Path) -> tuple[dict[str, dict[str, str]], dict[str, dict[str, float]]]:
    master: dict[str, dict[str, str]] = {}
    mp = mdir / "master.tsv"
    if mp.exists():
        lines = mp.read_text(encoding="utf-8", errors="replace").splitlines()
        hdr = lines[0].split("\t")
        for ln in lines[1:]:
            f = ln.split("\t")
            f += [""] * (len(hdr) - len(f))
            master[f[0]] = dict(zip(hdr, f))
    dist: dict[str, dict[str, float]] = {}
    dp = mdir / "distances.tsv"
    if dp.exists():
        lines = dp.read_text(encoding="utf-8", errors="replace").splitlines()
        hdr = lines[0].split("\t")
        for ln in lines[1:]:
            f = ln.split("\t")
            row: dict[str, float] = {}
            for h, v in zip(hdr[1:], f[1:]):
                if v:
                    try:
                        row[h[2:]] = float(v)
                    except ValueError:
                        pass
            dist[f[0]] = row
    return master, dist


def analyze(node: str, path: Path, master: dict, dist: dict) -> dict[str, Any]:
    has_prefix = node not in STANDALONE
    res: dict[str, Any] = {"log": path.name, "has_wall_prefix": has_prefix}

    # --- state ---------------------------------------------------------------------------
    n_lines = 0
    first_wall: dt.datetime | None = None
    last_wall: dt.datetime | None = None
    cur_ms = 0  # db0ais derived clock
    rx_counts: collections.Counter[str] = collections.Counter()
    types: collections.Counter[str] = collections.Counter()
    hop_h: collections.Counter[int] = collections.Counter()
    path_len: collections.Counter[int] = collections.Counter()
    orig_events: collections.Counter[str] = collections.Counter()
    orig_msgs: dict[str, set[str]] = collections.defaultdict(set)
    orig_fw: dict[str, tuple[int, str]] = {}
    orig_fw_cnt: dict[str, collections.Counter[tuple[int, str]]] = collections.defaultdict(collections.Counter)
    orig_hw: dict[str, int] = {}
    orig_airtime: collections.Counter[str] = collections.Counter()
    neigh: dict[str, dict[str, list[int]]] = collections.defaultdict(lambda: {"rssi": [], "snr": []})
    neigh_first: dict[str, dt.datetime] = {}
    rssi_all: list[int] = []
    rssi_minus_snr: list[int] = []
    rssi_minus_snr_pos: list[int] = []
    rssi_hist: collections.Counter[int] = collections.Counter()
    ack_rssi: list[int] = []
    ack_n = 0
    ack_h_bad = 0
    ack_h_cnt: collections.Counter[str] = collections.Counter()
    rx_airtime_decoded = 0.0
    air_by_type: collections.Counter[str] = collections.Counter()
    fw_events: collections.Counter[str] = collections.Counter()
    fw_air: collections.Counter[str] = collections.Counter()
    dup_flag: collections.Counter[str] = collections.Counter()
    own_flag: collections.Counter[str] = collections.Counter()
    srv_flag: collections.Counter[str] = collections.Counter()
    collections.Counter()
    delta_raw_cnt: collections.Counter[int] = collections.Counter()

    tx_n = 0
    tx_src: collections.Counter[str] = collections.Counter()
    tx_type: collections.Counter[str] = collections.Counter()
    tx_prio: collections.Counter[int] = collections.Counter()
    tx_src_type: collections.Counter[str] = collections.Counter()
    tx_len_air = 0.0
    tx_wait: list[int] = []
    tx_cad: collections.Counter[int] = collections.Counter()
    tx_air_by_hour: collections.Counter[str] = collections.Counter()
    collections.Counter()
    rly_reason: collections.Counter[str] = collections.Counter()
    gwi = gwu = 0

    err_rssi: list[int] = []
    err_snr: list[int] = []
    err_hour: collections.Counter[str] = collections.Counter()
    err_hist: collections.Counter[int] = collections.Counter()
    err_log_n = 0
    stale_rssi: list[int] = []
    err_ferr_n = 0

    cu_vals: list[int] = []
    cu_rx: list[int] = []
    cu_tx: list[int] = []
    cu_hour: dict[str, list[int]] = collections.defaultdict(list)
    cu_rx_mult = 0
    cu_window_est: list[float] = []

    markers: collections.Counter[str] = collections.Counter()
    gate_attempt: collections.Counter[int] = collections.Counter()
    cadfree_attempt: collections.Counter[int] = collections.Counter()
    gate_hour: collections.Counter[str] = collections.Counter()
    busy1_hour: collections.Counter[str] = collections.Counter()
    fp_hour: collections.Counter[str] = collections.Counter()
    storms: list[dict[str, Any]] = []
    storm_cur: dict[str, Any] | None = None
    onrx: list[int] = []
    sm_err: list[str] = []
    sm_counts: collections.Counter[str] = collections.Counter()
    ringst_max = {"queued": 0, "pending": 0, "retrying": 0}
    ringst_retry_n = 0
    ringst_n = 0
    ringw_src: collections.Counter[str] = collections.Counter()
    ringw_queued_max = 0
    ring_max = 0
    drop_prio: collections.Counter[str] = collections.Counter()
    drop_stale_prio: collections.Counter[str] = collections.Counter()
    txread_by_id: dict[str, list[tuple[str, str, str]]] = collections.defaultdict(list)
    dedup_slot_max = 0
    dedup_ring = 0

    stat_rows: list[dict[str, Any]] = []
    crash_hits: list[str] = []
    boot_hits: list[str] = []
    keep_id: dict[str, Any] = {}
    txframes_hw: collections.Counter[int] = collections.Counter()
    txframe_src: collections.Counter[str] = collections.Counter()
    txframes_hw_by_src: collections.Counter[tuple[str, int]] = collections.Counter()
    txframe_n = 0
    new_own: collections.Counter[tuple[str, str, str, str]] = collections.Counter()
    interleaved = 0

    # gateway / eth
    keeps: list[float] = []  # epoch-seconds-ish (derived clock)
    beats: list[float] = []
    cets_gw: list[float] = []
    gw_srv: list[str] = []
    gw_rx_types: collections.Counter[str] = collections.Counter()
    gw_other: collections.Counter[str] = collections.Counter()
    udp_nr: list[tuple[float, int, str]] = []
    eth_sums: list[dict[str, Any]] = []
    eth_stall: collections.Counter[str] = collections.Counter()
    eth_stall_ms: dict[str, list[int]] = collections.defaultdict(list)
    eth_events: list[dict[str, Any]] = []
    eth_ev_count: collections.Counter[str] = collections.Counter()
    eth_dhcp_rc: collections.Counter[str] = collections.Counter()
    ntp: collections.Counter[str] = collections.Counter()
    ntp_rtt: list[int] = []
    ntp_epochs: list[tuple[int, int]] = []  # (epoch, cur_ms)
    ntp_to: list[tuple[float, str]] = []
    ntp_lg: list[float] = []
    own_tx_check: collections.Counter[str] = collections.Counter()
    ext_counts: collections.Counter[str] = collections.Counter()
    ext_tx: list[tuple[int, int, int]] = []
    initial_lines: list[str] = []
    lora_cfg: list[str] = []

    def to_epoch(w: dt.datetime) -> float:
        return w.replace(tzinfo=dt.timezone.utc).timestamp()

    cur_wall: dt.datetime | None = None

    regime: dict[str, Any] = {"cur": None, "pend": None, "pend_n": 0, "runs": []}

    def note_delta(dd: int, w: dt.datetime) -> None:
        delta_raw_cnt[dd] += 1
        r60 = int(round(dd / 60.0)) * 60
        if regime["cur"] is None:
            regime["cur"] = r60
            regime["runs"].append([r60, w.strftime("%m-%d %H:%M:%S"), w.strftime("%m-%d %H:%M:%S"), 1])
            return
        if r60 == regime["cur"]:
            regime["pend"], regime["pend_n"] = None, 0
            regime["runs"][-1][2] = w.strftime("%m-%d %H:%M:%S")
            regime["runs"][-1][3] += 1
        else:
            if regime["pend"] == r60:
                regime["pend_n"] += 1
            else:
                regime["pend"], regime["pend_n"] = r60, 1
            if regime["pend_n"] >= 3:
                regime["cur"] = r60
                regime["runs"].append([r60, w.strftime("%m-%d %H:%M:%S"), w.strftime("%m-%d %H:%M:%S"), 3])
                regime["pend"], regime["pend_n"] = None, 0

    def hour_key(w: dt.datetime) -> str:
        return w.strftime("%m-%d %H")

    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fh:
        for raw in fh:
            n_lines += 1
            line = raw.rstrip("\r\n")
            if has_prefix:
                m = RE_PREFIX.match(line)
                if not m:
                    body = line
                else:
                    y, mo, d, hh, mi, ss, ms = (int(x) for x in m.groups())
                    cur_wall = dt.datetime(y, mo, d, hh, mi, ss, ms * 1000)
                    body = line[m.end():]
                    if first_wall is None:
                        first_wall = cur_wall
                    last_wall = cur_wall
            else:
                body = line
                if n_lines <= 40:
                    initial_lines.append(line)
                # derive wall clock from node ms counters
                tm = None
                if "t=" in body or "ts=" in body or ";ms;" in body:
                    tm = re.search(r"(?:\bt=|\bts=|;ms;)(\d+)\b", body)
                if tm and ("[LOG]" in body or "RX_ERROR" in body or "[ETH]" in body or "[GW]" in body or "RX_TIMEOUT" in body or "[EXT]" in body):
                    v = int(tm.group(1))
                    if v >= cur_ms - 5000:  # ignore interleaved garbage
                        cur_ms = v
                cur_wall = DB0AIS_BOOT0 + dt.timedelta(milliseconds=cur_ms)
                if first_wall is None:
                    first_wall = cur_wall
                last_wall = cur_wall
            now = cur_wall
            if now is None:
                continue
            ep = to_epoch(now)

            # ---- [LOG] family --------------------------------------------------------
            if "[LOG]" in body:
                if " TX x" in body:
                    m = RE_TX.match(body)
                    if m:
                        hh, mi, ss = int(m.group(1)), int(m.group(2)), int(m.group(3))
                        tx_n += 1
                        t_chr, prio, src = m.group(5), int(m.group(7)), m.group(8)
                        tx_src[src] += 1
                        tx_type[t_chr] += 1
                        tx_prio[prio] += 1
                        tx_src_type[f"{src}:{t_chr}"] += 1
                        ln_ = int(m.group(12))
                        a = toa_ms(ln_)
                        tx_len_air += a
                        tx_air_by_hour[hour_key(now)] += a
                        tx_wait.append(int(m.group(9)))
                        tx_cad[int(m.group(11))] += 1
                        if has_prefix:
                            dd = central_delta(hh * 3600 + mi * 60 + ss, now.hour * 3600 + now.minute * 60 + now.second)
                            note_delta(dd, now)
                    continue
                if " STAT " in body:
                    m = RE_STAT.match(body)
                    if m:
                        row: dict[str, Any] = {"wall": now.strftime("%Y-%m-%d %H:%M:%S"),
                                               "node_clock": f"{m.group(1)}:{m.group(2)}:{m.group(3)}"}
                        for kv in m.group(4).split():
                            if "=" in kv:
                                k, v = kv.split("=", 1)
                                row[k] = v
                        stat_rows.append(row)
                    continue
                if "[LOG] RLY" in body:
                    m = RE_RLY.search(body)
                    if m:
                        rly_reason[m.group(2)] += 1
                    continue
                if "[LOG] GWI" in body:
                    gwi += 1
                    continue
                if "[LOG] GWU" in body:
                    gwu += 1
                    continue
                if "[LOG] ERR" in body:
                    err_log_n += 1
                    m = RE_ERRL.match(body)
                    if m and int(m.group(7)) != 0:
                        err_ferr_n += 1
                    continue
                if "[LOG] DM " in body:
                    continue
                if " A x" in body and ("[LOG] 007 A" in body or "[LOG] 012 A" in body):
                    m = RE_ACK.match(body)
                    if m:
                        ack_n += 1
                        ack_rssi.append(int(m.group(7)))
                        h = int(m.group(6), 16)
                        ack_h_cnt[f"{h:02X}"] += 1
                        if not (0x80 <= h <= 0x84):
                            ack_h_bad += 1
                        rssi_hist[(int(m.group(7)) // 10) * 10] += 1
                        rx_airtime_decoded += toa_ms(int(m.group(4)))
                        air_by_type["ACK"] += toa_ms(int(m.group(4)))
                        dup_flag["ack_" + m.group(9)] += 1
                    continue
                m = RE_RX.match(body)
                if m:
                    hh, mi, ss = int(m.group(1)), int(m.group(2)), int(m.group(3))
                    ln_, t_chr = int(m.group(4)), m.group(5)
                    fcs = m.group(14)
                    if fcs == "0000":
                        rx_counts["fcs0000_excluded"] += 1
                        continue
                    if m.group(18) is None:
                        rx_counts["no_setlog_tail"] += 1
                        continue
                    msgid = m.group(6).upper()
                    hop = int(m.group(7), 16)
                    srv = m.group(8)
                    pd = m.group(11)
                    gt = pd.find(">")
                    pth = pd[:gt] if gt > 0 else pd
                    calls = [c for c in pth.split(",") if c]
                    if not calls:
                        rx_counts["empty_path"] += 1
                        continue
                    rssi, snr = int(m.group(18)), int(m.group(19))
                    dflag, oflag = m.group(20), m.group(21)
                    major, letter = int(m.group(15)), m.group(16)
                    hwv = int(m.group(12), 16) if re.fullmatch(r"[0-9A-Fa-f]+", m.group(12)) else -1
                    orig = calls[0]
                    last = calls[-1]
                    rx_counts["events"] += 1
                    types[t_chr] += 1
                    hop_h[hop] += 1
                    path_len[len(calls)] += 1
                    dup_flag[dflag] += 1
                    own_flag[oflag] += 1
                    srv_flag[srv] += 1
                    orig_events[orig] += 1
                    orig_msgs[orig].add(msgid)
                    orig_fw_cnt[orig][(major, letter)] += 1
                    orig_hw[orig] = hwv
                    a = toa_ms(ln_)
                    rx_airtime_decoded += a
                    air_by_type[t_chr] += a
                    orig_airtime[orig] += a
                    cls = fw_class(major, letter)
                    fw_events[cls] += 1
                    fw_air[cls] += a
                    rssi_all.append(rssi)
                    rssi_hist[(rssi // 10) * 10] += 1
                    if rssi < -60:
                        rssi_minus_snr.append(rssi - snr)
                        if snr >= 0:
                            rssi_minus_snr_pos.append(rssi - snr)
                    if last not in TIME_SERVERS:
                        nb = neigh[last]
                        nb["rssi"].append(rssi)
                        nb["snr"].append(snr)
                        if last not in neigh_first:
                            neigh_first[last] = now
                    else:
                        rx_counts["last_hop_is_time_server"] += 1
                    if has_prefix:
                        dd = central_delta(hh * 3600 + mi * 60 + ss, now.hour * 3600 + now.minute * 60 + now.second)
                        note_delta(dd, now)
                    else:
                        # db0ais: node clock vs derived (only meaningful after NTP sync)
                        if ntp["ok"] > 0:
                            dd = central_delta(hh * 3600 + mi * 60 + ss, now.hour * 3600 + now.minute * 60 + now.second)
                            note_delta(dd, now)
                    if not has_prefix:
                        pass
                continue

            # ---- markers (MC-DBG) -----------------------------------------------------
            if body.startswith("[MC-DBG]"):
                mk = RE_MARK.match(body)
                name = mk.group(1) if mk else "?"
                markers[name] += 1
                if name == "CHANNEL_UTIL":
                    m = RE_CU.search(body)
                    if m:
                        rx_, tx_, u = int(m.group(1)), int(m.group(2)), int(m.group(3))
                        cu_vals.append(u)
                        cu_rx.append(rx_)
                        cu_tx.append(tx_)
                        cu_hour[hour_key(now)].append(u)
                        if rx_ > 0 and abs(rx_ / 2476.0 - round(rx_ / 2476.0)) < 0.004:
                            cu_rx_mult += 1
                        if u > 0:
                            cu_window_est.append((rx_ + tx_) / (u / 100.0))
                elif name == "RX_ERROR":
                    m = RE_RXERR.search(body)
                    if m:
                        err_rssi.append(int(m.group(1)))
                        err_hist[(int(m.group(1)) // 10) * 10] += 1
                        err_snr.append(int(m.group(2)))
                        err_hour[hour_key(now)] += 1
                elif name == "CRC_ERROR":  # ESP32 family: the RX error carries its rssi/snr here (nRF52: RX_ERROR)
                    m = RE_CRCERR.search(body)
                    if m:
                        err_rssi.append(int(m.group(1)))
                        err_hist[(int(m.group(1)) // 10) * 10] += 1
                        err_snr.append(int(m.group(2)))
                        err_hour[hour_key(now)] += 1
                elif name == "RX_IRQ_STALE":
                    m = RE_STALE.search(body)
                    if m:
                        stale_rssi.append(int(m.group(1)))
                elif name == "ONRXDONE_TIME":
                    m = RE_ONRX.search(body)
                    if m:
                        onrx.append(int(m.group(1)))
                elif name == "TX_GATE_ENTER":
                    m = RE_GATE.search(body)
                    if m:
                        a = int(m.group(2))
                        gate_attempt[a] += 1
                        gate_hour[hour_key(now)] += 1
                        if a >= 3 and storm_cur is None:
                            storm_cur = {"start": now, "max_attempt": a, "gates": 1}
                        elif storm_cur is not None:
                            storm_cur["max_attempt"] = max(storm_cur["max_attempt"], a)
                            storm_cur["gates"] += 1
                elif name == "CAD_FREE":
                    m = RE_CADFREE.search(body)
                    if m:
                        cadfree_attempt[int(m.group(1))] += 1
                    if storm_cur is not None:
                        storm_cur["end"] = now
                        storm_cur["dur_s"] = round((now - storm_cur["start"]).total_seconds(), 2)
                        storm_cur["start"] = storm_cur["start"].strftime("%m-%d %H:%M:%S")
                        storm_cur["end"] = storm_cur["end"].strftime("%m-%d %H:%M:%S")
                        storms.append(storm_cur)
                        storm_cur = None
                elif name == "CAD_BUSY_1":
                    busy1_hour[hour_key(now)] += 1
                elif name == "CAD_FALSE_POSITIVE":
                    fp_hour[hour_key(now)] += 1
                elif name == "RING_STATUS":
                    m = RE_RINGST.search(body)
                    if m:
                        q, p, r, d_ = (int(m.group(i)) for i in range(1, 5))
                        ringst_n += 1
                        ringst_max["queued"] = max(ringst_max["queued"], q)
                        ringst_max["pending"] = max(ringst_max["pending"], p)
                        ringst_max["retrying"] = max(ringst_max["retrying"], r)
                        if r > 0:
                            ringst_retry_n += 1
                elif name == "RING_WRITE":
                    m = RE_RINGW.search(body)
                    if m:
                        ringw_src[f"{m.group(8)}:{m.group(2)}"] += 1
                        ringw_queued_max = max(ringw_queued_max, int(m.group(6)))
                        ring_max = int(m.group(7))
                elif name == "RING_TX_READ":
                    m = RE_TXREAD.search(body)
                    if m:
                        txread_by_id[m.group(6)].append((m.group(1), m.group(4), m.group(3)))
                elif name in ("RING_DROP_STALE", "RING_DROP_PRIO", "RING_DROP_NEW", "RING_DROP", "RING_OVERFLOW"):
                    pm = re.search(r"prio=(\d)", body)
                    tm_ = re.search(r"type=(\w+)", body)
                    key = f"prio{pm.group(1) if pm else '?'}" + (f"/type{tm_.group(1)}" if tm_ else "")
                    if name == "RING_DROP_STALE":
                        drop_stale_prio[key + ("/" + (re.search(r"reason=(\w+)", body).group(1) if "reason=" in body else ""))] += 1
                    else:
                        drop_prio[name + ":" + key] += 1
                elif name == "RX_DEDUP_ADD":
                    m = RE_SLOT.search(body)
                    if m:
                        dedup_slot_max = max(dedup_slot_max, int(m.group(1)))
                        dedup_ring = int(m.group(2))
                continue

            # ---- state machine ----------------------------------------------------------
            if body.startswith("[MC-SM]"):
                m = re.match(r"\[MC-SM\] (\w+) -> (\w+) rc=(-?\d+)\s*$", body)
                if m:
                    key = f"{m.group(1)} -> {m.group(2)} rc={m.group(3)}"
                    sm_counts[key] += 1
                    if m.group(3) != "0" and not (m.group(1) == "TX_PREPARE" and m.group(2) == "IDLE" and m.group(3) == "-1"):
                        if len(sm_err) < 20:
                            sm_err.append(f"{now:%m-%d %H:%M:%S} {key}")
                elif "ERROR" in body or "rc=" in body:
                    interleaved += 1
                    if "ERROR" in body and len(sm_err) < 20:
                        sm_err.append(f"{now:%m-%d %H:%M:%S} {body[:100]}")
                continue

            # ---- other lines ---------------------------------------------------------------
            if body[9:13] == "NEW-":
                m = RE_NEW.match(body)
                if m:
                    new_own[(m.group(4), m.group(1), m.group(5), f"{m.group(7)}:{m.group(8)}")] += 1
                continue
            if body.startswith("[GW];"):
                parts = body.split(";")
                kind = parts[1] if len(parts) > 1 else "?"
                mm = RE_GWMS.search(body)
                if mm and not has_prefix:
                    v = int(mm.group(1))
                    if v >= cur_ms - 5000:
                        cur_ms = v
                        now = DB0AIS_BOOT0 + dt.timedelta(milliseconds=cur_ms)
                        ep = to_epoch(now)
                if kind == "keep":
                    if len(parts) > 3 and parts[3] == "ok":
                        keeps.append(ep)
                    else:
                        gw_other[body[:60]] += 1
                elif kind == "rx":
                    typ = parts[3] if len(parts) > 3 else "?"
                    gw_rx_types[typ] += 1
                    if typ == "BEAT":
                        beats.append(ep)
                    elif typ == "CET":
                        cets_gw.append(ep)
                elif kind == "srv":
                    gw_srv.append(body)
                else:
                    gw_other[body[:60]] += 1
                continue
            if body.startswith("[KEEP]"):
                m = RE_KEEPID.match(body)
                if m and not keep_id:
                    keep_id = {"gw_id": m.group(1), "call": m.group(2), "version": m.group(3), "rest": m.group(4)}
                continue
            if body.startswith("[ETH]"):
                m = RE_ETHSUM.match(body)
                if m:
                    g = m.groups()
                    eth_sums.append({
                        "wall": now.strftime("%m-%d %H:%M:%S"), "ep": ep, "state": g[0], "link": int(g[1]), "link_age_s": int(g[2]),
                        "ip": g[3], "dest": g[4], "hb_age_s": int(g[5]), "got_ip_n": int(g[6]), "downs": int(g[7]),
                        "renews": int(g[8]), "renew_fail": int(g[9]), "resets": int(g[10]), "rx_n": int(g[11]),
                        "rx_max_ms": int(g[12]), "tx_fail": int(g[13]), "tx_max_ms": int(g[14]), "ms": int(g[15]),
                    })
                    if not has_prefix:
                        cur_ms = max(cur_ms, int(g[15]))
                    continue
                m = RE_ETHSTALL.match(body)
                if m:
                    eth_stall[f"{m.group(1)}/{m.group(3)}"] += 1
                    eth_stall_ms[f"{m.group(1)}/{m.group(3)}"].append(int(m.group(2)))
                    if m.group(1) in ("dhcp_begin", "hw_init", "ntp", "udp_tx", "udp_rx") and len(eth_events) < 60:
                        eth_events.append({"wall": now.strftime("%m-%d %H:%M:%S"), "line": body[:140]})
                    continue
                m = RE_ETHEV.match(body)
                if m:
                    eth_ev_count[m.group(1)] += 1
                    if m.group(1) == "dhcp":
                        rcm = re.search(r"rc;(-?\d+)", body)
                        eth_dhcp_rc[rcm.group(1) if rcm else "?"] += 1
                    if len(eth_events) < 60:
                        eth_events.append({"wall": now.strftime("%m-%d %H:%M:%S"), "line": body[:140]})
                    mm = re.search(r";ms;(\d+)", body)
                    if mm and not has_prefix:
                        cur_ms = max(cur_ms, int(mm.group(1)))
                    continue
                if len(eth_events) < 60:
                    eth_events.append({"wall": now.strftime("%m-%d %H:%M:%S"), "line": body[:140]})
                continue
            if body.startswith("[UDP]"):
                m = RE_UDPNR.match(body)
                if m:
                    udp_nr.append((ep, int(m.group(1)), m.group(2)))
                continue
            if body.startswith("[NTP]"):
                m = RE_NTP.match(body)
                if m:
                    ntp[m.group(1)] += 1
                    if m.group(1) == "ok":
                        mm = re.search(r"epoch;(\d+);rtt;(\d+)", body)
                        if mm:
                            ntp_rtt.append(int(mm.group(2)))
                            ntp_epochs.append((int(mm.group(1)), cur_ms))
                            ntp_lg.append(ep - int(mm.group(1)))
                    else:
                        ntp_to.append((ep, body[:80]))
                continue
            if body.startswith("OWN-TX-CHECK-UDP"):
                mm = re.search(r"check:(-?\d+)", body)
                own_tx_check[mm.group(1) if mm else "?"] += 1
                continue
            if body.startswith("[EXT]"):
                if body.startswith("[EXT];tx;len;"):
                    mm = re.match(r"\[EXT\];tx;len;(\d+);stack_hwm;(\d+);ms;(\d+)", body)
                    if mm:
                        ext_tx.append((int(mm.group(1)), int(mm.group(2)), int(mm.group(3))))
                        ext_counts["tx"] += 1
                elif body.startswith("[EXT] Out:"):
                    ext_counts["out"] += 1
                    mm = re.search(r'"src_type":"(\w+)","type":"(\w+)"', body)
                    if mm:
                        ext_counts[f"out_{mm.group(1)}_{mm.group(2)}"] += 1
                elif body.startswith("[EXT] Tele-Out"):
                    ext_counts["tele_out"] += 1
                else:
                    ext_counts["other"] += 1
                continue
            if body.startswith("[CET]"):
                parts = body.split(";")
                markers["CET_" + (parts[1] if len(parts) > 1 else "?")] += 1
                continue
            if body.startswith("[LoRa]") or body.startswith("...POWER"):
                if len(lora_cfg) < 12:
                    lora_cfg.append(body[:80])
                continue
            m = RE_TXFRAME.match(body[0:] if body.startswith("[MC-TEST]") else "")
            if m:
                txframe_n += 1
                hx = m.group(2)
                try:
                    b = bytes.fromhex(hx)
                    if len(b) >= 16 and b[0] != 0x41 and b[-1] == 0x7E and b[-9] == 0x00:
                        gtp = b.find(b">", 6)
                        first = b[6:gtp].split(b",")[0].decode("ascii", "replace") if gtp > 0 else ""
                        txframe_src[first] += 1
                        txframes_hw_by_src[(first, b[-8])] += 1
                except ValueError:
                    pass
                continue
            if body.startswith("[MC-TEST]"):
                continue
            if RE_BOOT.search(body) and "[LOG]" not in body:
                if len(boot_hits) < 20:
                    boot_hits.append(f"{now:%m-%d %H:%M:%S} {body[:90]}")
                continue
            if RE_CRASH.search(body) and "[LOG]" not in body and not body.startswith("["):
                if len(crash_hits) < 20:
                    crash_hits.append(f"{now:%m-%d %H:%M:%S} {body[:120]}")
                continue
            if body.startswith("[MC-WARN]"):
                markers["MC-WARN:" + body.split()[1] if len(body.split()) > 1 else "MC-WARN"] += 1
                continue
            if body.startswith("[PING]"):
                markers["PING_send"] += 1
                continue
            if body.startswith("[RX];drop"):
                markers["RX_drop_" + body.split(";")[2] if len(body.split(";")) > 2 else "RX_drop"] += 1
                continue
            if body.startswith("APRS decode"):
                markers["APRS_decode_err:" + re.sub(r"\d+", "N", body[:60])] += 1
                continue
            if body.startswith("OnRxError"):
                markers["OnRxError"] += 1
            elif body.startswith("OnRxDone"):
                markers["OnRxDone"] += 1
            elif body.startswith("OnTXDone"):
                markers["OnTXDone"] += 1
            elif body.startswith("[HEAP]") or ";[HEAP]" in body:
                markers["HEAP_line"] += 1
            elif "[WIFI" in body:
                markers["WIFI_line"] += 1
            elif body.startswith("[SETST]"):
                markers["SETST"] += 1
            elif "TimeClient" in body:
                markers["TimeClient"] += 1

    # ---------------------------------------------------------------- derived values ----
    # firmware per originator: most frequent (major, letter) pair, ignoring the '#' placeholder when a real letter was seen
    for o_, cnt_ in orig_fw_cnt.items():
        real = [(k, c) for k, c in cnt_.items() if k[1] != "#"]
        pick = max(real, key=lambda kc: kc[1])[0] if real else cnt_.most_common(1)[0][0]
        orig_fw[o_] = pick
    span_s = (last_wall - first_wall).total_seconds() if first_wall and last_wall else 0.0
    res["span"] = {"first": first_wall.strftime("%Y-%m-%d %H:%M:%S") if first_wall else None,
                   "last": last_wall.strftime("%Y-%m-%d %H:%M:%S") if last_wall else None,
                   "hours": round(span_s / 3600.0, 2), "lines": n_lines,
                   "clock_basis": "logger wall prefix" if has_prefix else f"derived from node millis + PuTTY start {STANDALONE_START[11:]} (CET)"}

    # identity
    hw_id = None
    own_call = keep_id.get("call")
    for (src_c, hwb), c in txframes_hw_by_src.items():
        if src_c == own_call:
            txframes_hw[hwb] += c
    new_hw: collections.Counter[int] = collections.Counter()
    for (c_, k_, hwx, fwx), n_ in new_own.items():
        if c_ == own_call and re.fullmatch(r"[0-9A-Fa-f]+", hwx):
            new_hw[int(hwx, 16)] += n_
    if txframes_hw:
        hw_id = txframes_hw.most_common(1)[0][0]
    elif new_hw:
        hw_id = new_hw.most_common(1)[0][0]
    res["identity"] = {
        "callsign": keep_id.get("call"), "gw_id": keep_id.get("gw_id"), "version_keep": keep_id.get("version"),
        "keep_rest": keep_id.get("rest"), "hw_id": hw_id, "hw_name": HW_NAMES.get(hw_id) if hw_id is not None else None,
        "hw_votes": dict(txframes_hw), "hw_votes_NEW_lines": dict(new_hw), "own_new_fw": sorted({(k[3]) for k in new_own if k[0] == own_call}), "tx_frames_seen": txframe_n, "own_originated_tx_frames": sum(txframes_hw.values()), "tx_frame_first_call_top": dict(txframe_src.most_common(3)), "lora_cfg_lines": lora_cfg,
        "gw_srv_lines": gw_srv[:3],
        "own_gateway_role": {"keepalive_ok": len(keeps), "gwu_upload": gwu, "gwi_inject": gwi, "beat_rx": len(beats)},
    }
    stat_fw = collections.Counter(r.get("fw") for r in stat_rows)
    res["identity"]["stat_fw"] = dict(stat_fw)

    # node clock offset
    if delta_raw_cnt:
        rounded: collections.Counter[int] = collections.Counter()
        for d_, c in delta_raw_cnt.items():
            rounded[int(round(d_ / 60.0)) * 60] += c
        mode = rounded.most_common(1)[0][0]
        res["clock"] = {"regimes": regime["runs"], "offset_node_minus_logger_s_mode": mode, "offset_dist_s": dict(rounded.most_common(6)),
                        "raw_delta_range_s": [min(delta_raw_cnt), max(delta_raw_cnt)],
                        "samples": sum(delta_raw_cnt.values())}
    else:
        res["clock"] = {}

    # NTP anchor drift check (db0ais mainly)
    if ntp_epochs and not has_prefix:
        boots = [(e - ms / 1000.0) for e, ms in ntp_epochs if ms > 0]
        res["clock"]["ntp_boot_epoch_spread_s"] = [round(min(boots), 1), round(max(boots), 1)] if boots else None
        res["clock"]["ntp_first_epoch_utc"] = dt.datetime.fromtimestamp(ntp_epochs[0][0], dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    elif ntp_epochs and has_prefix:
        # compare first/last epoch with logger prefix is done by timeline of ntp lines only (count)
        res["clock"]["logger_minus_utc_s_from_ntp"] = {"median": round(statistics.median(ntp_lg)), "min": round(min(ntp_lg)), "max": round(max(ntp_lg))}
        res["clock"]["ntp_epoch_first_utc"] = dt.datetime.fromtimestamp(ntp_epochs[0][0], dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")

    # traffic
    ev = rx_counts["events"]
    type_names = {":": "text(3A)", "@": "heartbeat/HEY(40)", "!": "position(21)", "A": "ack", "D": "?", "<": "?"}
    res["traffic"] = {
        "heard_events": ev,
        "fcs0000_excluded": rx_counts["fcs0000_excluded"],
        "no_setlog_tail": rx_counts["no_setlog_tail"],
        "ack_events": ack_n,
        "ack_h_outside_80_84": ack_h_bad,
        "ack_h_top": dict(ack_h_cnt.most_common(6)),
        "dup_flag": dict(dup_flag),
        "own_in_path": dict(own_flag),
        "srv_flag": dict(srv_flag),
        "types": {k: {"n": v, "pct": round(100.0 * v / ev, 1) if ev else 0, "label": type_names.get(k, "?")} for k, v in types.most_common()},
        "unique_originators": len(orig_events),
        "unique_originators_excl_time_server": len([o for o in orig_events if o not in TIME_SERVERS]),
        "time_server_events": {o: orig_events[o] for o in TIME_SERVERS if o in orig_events},
        "unique_msgids": len({(o, m) for o, s in orig_msgs.items() for m in s}),
        "hop_H_field": {f"H{k:02d}": v for k, v in sorted(hop_h.items())},
        "hop_H_pct": {f"H{k:02d}": round(100.0 * v / ev, 1) for k, v in sorted(hop_h.items())} if ev else {},
        "path_len": {str(k): v for k, v in sorted(path_len.items())},
        "path_len_pct": {str(k): round(100.0 * v / ev, 1) for k, v in sorted(path_len.items())} if ev else {},
        "direct_zero_relay_pct": round(100.0 * path_len.get(1, 0) / ev, 1) if ev else 0,
        "rx_airtime_decoded_s": round(rx_airtime_decoded / 1000.0, 0),
        "airtime_share_by_type_pct": {k: round(100.0 * v / rx_airtime_decoded, 1) for k, v in air_by_type.most_common()} if rx_airtime_decoded else {},
        "ack_share_of_all_frames_pct": round(100.0 * ack_n / (ack_n + ev), 1) if (ack_n + ev) else 0,
    }
    # own TX
    res["own_tx"] = {
        "tx_lines": tx_n, "radio_tx_markers": markers.get("RADIO_TX", 0), "tx_done": markers.get("OnTXDone", 0),
        "src_split": dict(tx_src), "src_labels": {"o": "own", "r": "relay", "g": "gateway(server)"},
        "src_pct": {k: round(100.0 * v / tx_n, 1) for k, v in tx_src.items()} if tx_n else {},
        "type_split": dict(tx_type), "prio_split": {str(k): v for k, v in sorted(tx_prio.items())},
        "src_type": dict(tx_src_type), "airtime_s": round(tx_len_air / 1000.0, 1),
        "wait_ms": summ([float(x) for x in tx_wait], 0),
        "cad_at_tx": {str(k): v for k, v in sorted(tx_cad.items())},
        "relay_decisions_RLY": dict(rly_reason.most_common()),
        "tx_per_hour": round(tx_n / (span_s / 3600.0), 1) if span_s else None,
    }

    # neighbours
    nb_rows = []
    for c, d_ in neigh.items():
        r, s = d_["rssi"], d_["snr"]
        mi = master.get(c, {})
        nb_rows.append({
            "call": c, "n": len(r), "rssi_median": round(pct(r, 50), 1), "rssi_p10": round(pct(r, 10), 1),
            "rssi_p90": round(pct(r, 90), 1), "rssi_max": max(r), "snr_median": round(pct(s, 50), 1),
            "hw": mi.get("hardware") or (HW_NAMES.get(orig_hw.get(c, -1)) if c in orig_hw else None),
            "fw_log": f"{orig_fw[c][0]}:{orig_fw[c][1]}" if c in orig_fw else None,
            "fw_master": mi.get("firmware"), "desc": (mi.get("description") or mi.get("name") or "")[:40],
            "lat": mi.get("lat"), "lon": mi.get("lon"), "isGateway": mi.get("isGateway"),
            "dist_km": dist.get(c, {}).get(node),
        })
    nb_rows.sort(key=lambda x: -x["n"])
    res["neighbours"] = {"n_direct": len(nb_rows), "top": nb_rows[:25], "all_calls": [r["call"] for r in nb_rows]}
    # 12-byte ACK frames print no OnRxDone line (only ONRXDONE_TIME and a [LOG] ACK line): one RX event per
    # ONRXDONE_TIME; logs without that marker fall back to the OnRxDone lines
    rx_good_events = len(onrx) if onrx else markers.get("OnRxDone", 0)
    rx_good_src = "ONRXDONE_TIME" if onrx else "OnRxDone"
    over20 = sum(1 for r in rssi_all if r > -20)
    over40 = sum(1 for r in rssi_all if r > -40)
    res["rf"] = {
        "rssi_hist_10dB": {str(k): v for k, v in sorted(rssi_hist.items())},
        "rssi_all": summ([float(x) for x in rssi_all]),
        "rx_rssi_gt_minus20": over20, "rx_rssi_gt_minus40": over40, "ack_rssi": summ([float(x) for x in ack_rssi]),
        "noise_rssi_minus_snr_all": summ([float(x) for x in rssi_minus_snr]),
        "noise_rssi_minus_snr_snr_ge0": summ([float(x) for x in rssi_minus_snr_pos]),
        "rx_error_hist_10dB": {str(k): v for k, v in sorted(err_hist.items())},
        "rx_error_gt_minus85": sum(1 for x in err_rssi if x > -85),
        "rx_error_gt_minus60": sum(1 for x in err_rssi if x > -60),
        "rx_error_n": len(err_rssi), "rx_error_log_ERR_n": err_log_n, "rx_error_ferr_nonzero": err_ferr_n,
        "rx_error_rssi": summ([float(x) for x in err_rssi]), "rx_error_snr": summ([float(x) for x in err_snr]),
        "rx_error_per_hour_mean": round(len(err_rssi) / (span_s / 3600.0), 1) if span_s else None,
        "rx_error_share_of_rx_pct": round(100.0 * len(err_rssi) / (len(err_rssi) + rx_good_events), 1) if err_rssi else 0,
        "rx_good_events": rx_good_events, "rx_good_events_source": rx_good_src,
        "rx_irq_stale": {"rssi_lines": markers.get("RX_IRQ_STALE", 0),
                         "note": "no RX_IRQ_STALE markers present; anchored patterns checked separately" if not markers.get("RX_IRQ_STALE")
                         else "ESP32 stale-IRQ markers present (rssi= lines and 'forced restart' lines)",
                         **({"rssi": summ([float(x) for x in stale_rssi]), "early": markers.get("RX_IRQ_STALE_EARLY", 0)}
                            if markers.get("RX_IRQ_STALE") else {})},
        "theory_floor_bw250_dbm": -114,
    }

    # channel
    util_by_hour = {h: round(statistics.fmean(v), 1) for h, v in sorted(cu_hour.items())}
    stat_util = [float(r["util"]) for r in stat_rows if "util" in r]
    stat_rx_ms = sum(int(r["rx"]) for r in stat_rows if "rx" in r)
    stat_tx_ms = sum(int(r["tx"]) for r in stat_rows if "tx" in r)
    half = len(cu_vals) // 2
    res["channel"] = {
        "CHANNEL_UTIL": summ([float(x) for x in cu_vals], 1) | {
            "ge70": sum(1 for x in cu_vals if x >= 70), "ge90": sum(1 for x in cu_vals if x >= 90),
            "first_half_mean": round(statistics.fmean(cu_vals[:half]), 1) if half else None,
            "second_half_mean": round(statistics.fmean(cu_vals[half:]), 1) if half else None,
            "window_est_s": round(statistics.median(cu_window_est) / 1000.0, 1) if cu_window_est else None,
        },
        "util_by_hour": util_by_hour,
        "inflation_check": {"rx_values_multiple_of_2476ms": cu_rx_mult, "rx_values_nonzero": sum(1 for x in cu_rx if x > 0),
                            "verdict": "no inflation fingerprint" if cu_rx_mult < 0.05 * max(1, sum(1 for x in cu_rx if x > 0)) else "INFLATED"},
        "STAT_util_5min": summ(stat_util, 1),
        "STAT_rx_ms_total": stat_rx_ms, "STAT_tx_ms_total": stat_tx_ms,
        "STAT_mean_util_from_airtime_pct": round(100.0 * (stat_rx_ms + stat_tx_ms) / (len(stat_rows) * 300000.0), 1) if stat_rows else None,
        "own_tx_airtime_s_from_TX_lines": round(tx_len_air / 1000.0, 0),
        "own_tx_airtime_s_from_CHANNEL_UTIL": round(sum(cu_tx) / 1000.0, 0),
        "own_tx_airtime_s_from_STAT": round(stat_tx_ms / 1000.0, 0),
        "rx_airtime_s_decoded_frames": round(rx_airtime_decoded / 1000.0, 0),
        "rx_airtime_s_from_STAT": round(stat_rx_ms / 1000.0, 0),
        "stat_rows": len(stat_rows),
    }

    # CAD
    scans = markers.get("CAD_SCAN", 0)
    b1 = markers.get("CAD_BUSY_1", 0)
    bc = markers.get("CAD_BUSY", 0)
    fp = markers.get("CAD_FALSE_POSITIVE", 0)
    gates = sum(gate_attempt.values())
    gsum = {str(k): v for k, v in sorted(gate_attempt.items())}
    g_pct = {str(k): round(100.0 * v / gates, 1) for k, v in sorted(gate_attempt.items())} if gates else {}
    ge3 = sum(v for k, v in gate_attempt.items() if k >= 3)
    sdur = [s["dur_s"] for s in storms]
    res["cad"] = {
        "cad_scans": scans, "cad_busy_1_initial": b1, "cad_busy_confirmed": bc, "cad_false_positive": fp,
        "initial_busy_rate_pct": round(100.0 * b1 / scans, 1) if scans else None,
        "false_positive_pct_of_initial_busy": round(100.0 * fp / b1, 1) if b1 else None,
        "confirmed_busy_pct_of_initial_busy": round(100.0 * bc / b1, 1) if b1 else None,
        "tx_gate_enter": gates, "cad_attempt_dist": gsum, "cad_attempt_pct": g_pct,
        "attempt0_pct": g_pct.get("0"), "attempt_ge3_pct": round(100.0 * ge3 / gates, 1) if gates else None,
        "attempt_max": max(gate_attempt) if gate_attempt else None,
        "cad_free_attempt_dist": {str(k): v for k, v in sorted(cadfree_attempt.items())},
        "cad_free": markers.get("CAD_FREE", 0), "cad_free_no_tx": markers.get("CAD_FREE_NO_TX", 0),
        "cad_safety_timeout": markers.get("CAD_SAFETY_TIMEOUT", 0),
        "storm_episodes_attempt_ge3": len(storms), "storm_dur_s": summ(sdur, 1),
        "storm_longest": sorted(storms, key=lambda s: -s["dur_s"])[:3],
        "csma_aged": markers.get("CSMA_AGED", 0),
        "per_hour": {h: {"gates": gate_hour[h], "busy1": busy1_hour[h], "fp": fp_hour[h], "rx_err": err_hour[h],
                         "util": util_by_hour.get(h)} for h in sorted(set(gate_hour) | set(util_by_hour))},
    }
    # ONRXDONE
    res["onrxdone"] = {"ms": summ([float(x) for x in onrx], 1), "gt50ms": sum(1 for x in onrx if x > 50),
                       "slow_warn_markers": markers.get("MC-WARN:ONRXDONE_SLOW", 0),
                       "stats_max_ms": markers.get("ONRXDONE_STATS", 0),
                       "stat_onrx_max_ms": max((int(r["onrx_max"]) for r in stat_rows if "onrx_max" in r), default=None),
                       "stat_onrx_p99_median": statistics.median([int(r["onrx_p99"]) for r in stat_rows if "onrx_p99" in r]) if any("onrx_p99" in r for r in stat_rows) else None}
    # state machine
    res["state_machine"] = {
        "transitions": dict(sm_counts.most_common(12)),
        "csma_backoff_rc_minus1": sm_counts.get("TX_PREPARE -> IDLE rc=-1", 0),
        "errors_other_rc": sm_err, "errors_other_n": len(sm_err), "interleaved_garbage_lines": interleaved,
    }
    # ring
    dbl = []
    for mid, lst in txread_by_id.items():
        if len(lst) > 1:
            dbl.append((mid, lst))
    bugtype = [mid for mid, lst in dbl if len(lst) >= 2 and lst[0][0] == lst[1][0] and lst[0][1] == "00" and lst[1][1] == "FF"]
    res["ring"] = {
        "ring_status_samples": ringst_n, "max": ringst_max, "retrying_gt0_samples": ringst_retry_n,
        "ring_write_by_src_type": dict(ringw_src.most_common(10)), "ring_write_queued_max": ringw_queued_max, "MAX_RING": ring_max,
        "drops_non_stale": dict(drop_prio), "drop_stale_by_prio_reason": dict(drop_stale_prio),
        "supersede": markers.get("RING_SUPERSEDE", 0), "supersede_keep": markers.get("RING_SUPERSEDE_KEEP", 0),
        "ring_overflow": markers.get("RING_OVERFLOW", 0),
        "relay_loop_blocked": markers.get("RELAY_LOOP_BLOCKED", 0), "retransmit_giveup": markers.get("RETRANSMIT_GIVEUP", 0),
        "ack_reject": markers.get("ACK_REJECT", 0), "relay_queued": markers.get("RELAY_QUEUED", 0),
        "ring_tx_read_unique_ids": len(txread_by_id), "ring_tx_read_ids_gt1": len(dbl),
        "double_send_bug_type_00_then_FF_same_slot": len(bugtype),
        "dedup": {"new": markers.get("RX_DEDUP_NEW", 0), "dup": markers.get("RX_DEDUP_DUP", 0), "add": markers.get("RX_DEDUP_ADD", 0),
                  "ring_size": dedup_ring, "max_slot_seen": dedup_slot_max},
        "overhear_cancel_NBR": None,
    }

    # STAT aggregate
    def sumk(k: str) -> int:
        return sum(int(r[k]) for r in stat_rows if k in r and r[k].lstrip("-").isdigit())

    ups = [int(r["up"]) for r in stat_rows if "up" in r and r["up"].isdigit()]
    tms = [int(r["t"]) for r in stat_rows if "t" in r and r["t"].isdigit()]
    reboots = []
    for i in range(1, len(ups)):
        if ups[i] + 5 < ups[i - 1]:
            reboots.append({"before": stat_rows[i - 1]["wall"], "after": stat_rows[i]["wall"], "up_before": ups[i - 1], "up_after": ups[i]})
    for i in range(1, len(tms)):
        if tms[i] + 5000 < tms[i - 1] and not any(r["after"] == stat_rows[i]["wall"] for r in reboots):
            reboots.append({"before": stat_rows[i - 1]["wall"], "after": stat_rows[i]["wall"], "t_before": tms[i - 1], "t_after": tms[i]})
    heaps = [int(r["heap"]) for r in stat_rows if "heap" in r and r["heap"].isdigit()]
    dropv = [[int(x) for x in r["drop"].split("/")] for r in stat_rows if "drop" in r]
    ringmax_v = [int(r["ringmax"].split("/")[0]) for r in stat_rows if "ringmax" in r]
    ringmax_den = next((int(r["ringmax"].split("/")[1]) for r in stat_rows if "ringmax" in r), None)
    # slope of heap per hour (least squares)
    slope = None
    if len(heaps) > 10:
        xs = list(range(len(heaps)))
        mx, my = statistics.fmean(xs), statistics.fmean(heaps)
        den = sum((x - mx) ** 2 for x in xs)
        slope = round(sum((x - mx) * (y - my) for x, y in zip(xs, heaps)) / den * 12.0, 1) if den else 0
    res["stat"] = {
        "rows": len(stat_rows), "newid_sum": sumk("newid"), "dup_sum": sumk("dup"), "err_sum": sumk("err"),
        "txn_sum": sumk("txn"), "txfail_sum": sumk("txfail"),
        "drop_sum_prio1to5": [sum(d_[i] for d_ in dropv) for i in range(5)] if dropv else None,
        "ringmax_max": max(ringmax_v) if ringmax_v else None, "ringmax_of": ringmax_den,
        "ringmax_ge15_rows": sum(1 for v in ringmax_v if v >= 15),
        "mh_max": max((int(r["mh"]) for r in stat_rows if "mh" in r), default=None),
        "heap": {"min": min(heaps), "max": max(heaps), "first": heaps[0], "last": heaps[-1], "slope_per_hour": slope} if heaps else None,
        "up_first": ups[0] if ups else None, "up_last": ups[-1] if ups else None,
        "boot_wall_est": (dt.datetime.strptime(stat_rows[0]["wall"], "%Y-%m-%d %H:%M:%S") - dt.timedelta(seconds=ups[0])).strftime("%Y-%m-%d %H:%M:%S") if ups else None,
        "boot_before_capture_start_s": round((first_wall - (dt.datetime.strptime(stat_rows[0]["wall"], "%Y-%m-%d %H:%M:%S") - dt.timedelta(seconds=ups[0]))).total_seconds()) if ups and first_wall else None,
        "uptime_resets": reboots,
        "dedup_dup_share_pct": round(100.0 * sumk("dup") / max(1, sumk("dup") + sumk("newid")), 1),
    }
    # counters
    res["health"] = {
        "boot_banner_hits": boot_hits, "crash_hits": crash_hits,
        "heap_markers": markers.get("HEAP_line", 0), "wifi_markers": markers.get("WIFI_line", 0),
        "mc_warn": {k: v for k, v in markers.items() if k.startswith("MC-WARN")},
        "rx_drop_unconfigured": markers.get("RX_drop_unconfigured", 0),
        "aprs_decode_errors": {k: v for k, v in markers.items() if k.startswith("APRS_decode_err")},
        "ping_send_lines": markers.get("PING_send", 0),
    }

    # firmware census
    own_echo = orig_events.get(own_call, 0) if own_call else 0
    tot_nodes = {o for o in orig_events if o not in TIME_SERVERS and o != own_call}
    pre_nodes = {o for o in tot_nodes if fw_class(*orig_fw[o]) == "pre_cad"}
    fwver: collections.Counter[str] = collections.Counter(f"4.{orig_fw[o][0]}{orig_fw[o][1]}" for o in tot_nodes)
    ev_nonsrv = sum(c for o, c in orig_events.items() if o in tot_nodes)
    pre_ev = sum(orig_events[o] for o in pre_nodes)
    air_nonsrv = sum(a for o, a in orig_airtime.items() if o in tot_nodes)
    pre_air = sum(orig_airtime[o] for o in pre_nodes)
    pre_msgs = sum(len(orig_msgs[o]) for o in pre_nodes)
    all_msgs = sum(len(orig_msgs[o]) for o in tot_nodes)
    top_pre = []
    for o in sorted(pre_nodes, key=lambda x: (-orig_events[x], x))[:15]:  # ties by call (deterministic)
        mi = master.get(o, {})
        top_pre.append({"call": o, "fw": f"4.{orig_fw[o][0]}{orig_fw[o][1]}", "events": orig_events[o], "msgs": len(orig_msgs[o]),
                        "events_pct_of_nodes": round(100.0 * orig_events[o] / ev_nonsrv, 1) if ev_nonsrv else 0,
                        "hw": mi.get("hardware") or HW_NAMES.get(orig_hw.get(o, -1)), "desc": (mi.get("description") or mi.get("name") or "")[:36],
                        "dist_km": dist.get(o, {}).get(node), "lat": mi.get("lat"), "lon": mi.get("lon"), "fw_master": mi.get("firmware")})
    res["firmware_census"] = {
        "nodes_total": len(tot_nodes), "nodes_pre_cad": len(pre_nodes), "own_call_echo_events_excluded": own_echo,
        "nodes_unknown_4_35_hash": sorted(o for o in tot_nodes if fw_class(*orig_fw[o]) == "unknown_4.35"),
        "nodes_fw0_not_time_server": sorted(o for o in orig_events if orig_fw[o][0] == 0 and o not in TIME_SERVERS),
        "nodes_pre_cad_pct": round(100.0 * len(pre_nodes) / len(tot_nodes), 1) if tot_nodes else None,
        "events_total_excl_time_server": ev_nonsrv, "events_pre_cad": pre_ev,
        "events_pre_cad_pct": round(100.0 * pre_ev / ev_nonsrv, 1) if ev_nonsrv else None,
        "unique_msgs_pre_cad_pct": round(100.0 * pre_msgs / all_msgs, 1) if all_msgs else None,
        "airtime_pre_cad_pct": round(100.0 * pre_air / air_nonsrv, 1) if air_nonsrv else None,
        "fw_events_incl_server": dict(fw_events),
        "versions_nodes": dict(fwver.most_common(20)),
        "top_pre_cad": top_pre,
        "top_talkers_all": [{"call": o, "events": c, "msgs": len(orig_msgs[o]), "fw": f"4.{orig_fw[o][0]}{orig_fw[o][1]}",
                             "dist_km": dist.get(o, {}).get(node)} for o, c in orig_events.most_common(12)],
        "unique_originators_excl_own_and_time_server": len(tot_nodes),
    }

    # gateway / eth / server path
    def gaps(ts: list[float], thr: float) -> dict[str, Any]:
        g = [(b - a, a) for a, b in zip(ts, ts[1:]) if b - a > thr]
        allg = [b - a for a, b in zip(ts, ts[1:])]
        return {"n_gt_thr": len(g), "thr_s": thr, "max_s": round(max((x for x, _ in g), default=0), 1),
                "max_all_s": round(max(allg, default=0), 1), "median_all_s": round(statistics.median(allg), 1) if allg else None,
                "top": [{"gap_s": round(x, 1), "after": dt.datetime.fromtimestamp(a, dt.timezone.utc).strftime("%m-%d %H:%M:%S")}
                        for x, a in sorted(g, reverse=True)[:5]]}

    # BEAT reply tracking: per keepalive, was a BEAT seen within 5 s?
    events_merged = sorted([(t, "k") for t in keeps] + [(t, "b") for t in beats])
    pend: float | None = None
    replied = False
    misses: list[float] = []
    orphans = 0
    n_keep_eval = 0
    for t, k in events_merged:
        if k == "k":
            if pend is not None:
                n_keep_eval += 1
                if not replied:
                    misses.append(pend)
            pend, replied = t, False
        else:
            if pend is not None and t - pend < 5.0 and not replied:
                replied = True
            else:
                orphans += 1
    eps: list[dict[str, Any]] = []
    cets_sorted = sorted(cets_gw)
    for t in misses:
        if eps and t - eps[-1]["_last"] < 45.0:
            eps[-1]["_last"] = t
            eps[-1]["n"] += 1
        else:
            eps.append({"_start": t, "_last": t, "n": 1})
    # recovery = next BEAT after episode end
    bs = sorted(beats)
    out_eps = []
    for e in eps:
        nxt = next((b for b in bs if b > e["_last"] + 5.0), None)
        dur = (nxt - e["_start"]) if nxt else None
        cets_in = sum(1 for c in cets_sorted if e["_start"] <= c <= (nxt or e["_last"] + 30.0))
        out_eps.append({"start": dt.datetime.fromtimestamp(e["_start"], dt.timezone.utc).strftime("%m-%d %H:%M:%S"),
                        "missed_keepalives": e["n"], "no_beat_for_s": round(dur, 0) if dur else None, "cet_rx_inside": cets_in})
    udp_secs = [(t, n, s) for t, n, s in udp_nr]
    # cluster 'Server not responding' by time (gap > 120 s starts new cluster)
    clusters: list[dict[str, Any]] = []
    for t, n, s in udp_secs:
        if clusters and t - clusters[-1]["_last"] < 120:
            clusters[-1]["_last"] = t
            clusters[-1]["n"] += 1
            clusters[-1]["max_age_s"] = max(clusters[-1]["max_age_s"], n)
        else:
            clusters.append({"_start": t, "_last": t, "n": 1, "max_age_s": n})
    ucl = [{"start": dt.datetime.fromtimestamp(c["_start"], dt.timezone.utc).strftime("%m-%d %H:%M:%S"), "n": c["n"], "max_age_s": c["max_age_s"]}
           for c in clusters]
    hours_udp: collections.Counter[str] = collections.Counter(dt.datetime.fromtimestamp(t, dt.timezone.utc).strftime("%m-%d %H") for t, _, _ in udp_secs)
    miss_hour: collections.Counter[str] = collections.Counter(dt.datetime.fromtimestamp(t, dt.timezone.utc).strftime("%m-%d %H") for t in misses)
    exp_keeps = span_s / 30.0 if span_s else 0
    res["gateway"] = {
        "keepalive_ok": len(keeps), "keepalive_expected_30s": round(exp_keeps, 0),
        "keepalive_gaps": gaps(keeps, 45.0),
        "beat_rx": len(beats), "beat_after_keep_missing": len(misses), "beat_orphans": orphans,
        "beat_gaps_gt65s": gaps(beats, 65.0),
        "beat_miss_episodes_n": len(out_eps), "beat_miss_episodes": out_eps[:80],
        "beat_miss_per_hour": dict(sorted(miss_hour.items())), "beat_miss_pct_of_keepalives": round(100.0 * len(misses) / max(1, len(keeps)), 2),
        "cet_gw_rx": len(cets_gw), "gw_rx_types": dict(gw_rx_types), "gw_other": dict(gw_other.most_common(6)),
        "udp_server_not_responding_n": len(udp_secs),
        "udp_server_not_responding_variants": dict(collections.Counter(s for _, _, s in udp_secs)),
        "udp_age_s_max": max((n for _, n, _ in udp_secs), default=0),
        "udp_clusters_n": len(ucl), "udp_clusters": ucl[:40], "udp_per_hour": dict(sorted(hours_udp.items())),
        "ntp": {"ok": ntp["ok"], "timeout": ntp["timeout"], "rtt_ms": summ([float(x) for x in ntp_rtt], 0),
                "timeouts": [dt.datetime.fromtimestamp(t, dt.timezone.utc).strftime("%m-%d %H:%M:%S") for t, _ in ntp_to[:12]]},
        "own_tx_check_udp": dict(own_tx_check),
        "gw_srv": gw_srv[:3],
    }
    # ETH
    last_sum = eth_sums[-1] if eth_sums else None
    first_sum = eth_sums[0] if eth_sums else None
    hb = [s["hb_age_s"] for s in eth_sums]
    rx_inc = [b["rx_n"] - a["rx_n"] for a, b in zip(eth_sums, eth_sums[1:])]
    idx_zero = [i for i, v in enumerate(rx_inc) if v == 0]
    res["eth"] = {
        "present": bool(eth_sums or eth_stall or eth_ev_count),
        "summaries": len(eth_sums), "first": first_sum, "last": last_sum,
        "hb_age_s": summ([float(x) for x in hb], 0), "hb_age_gt35_n": sum(1 for x in hb if x > 35),
        "ips_seen": sorted({s["ip"] for s in eth_sums}), "dest_seen": sorted({s["dest"] for s in eth_sums}),
        "link_states": dict(collections.Counter(s["state"] for s in eth_sums)),
        "rx_n_increment_60s": summ([float(x) for x in rx_inc], 1), "rx_zero_increment_intervals": len(idx_zero),
        "stalls": {k: {"n": v, "ms": summ([float(x) for x in eth_stall_ms[k]], 0)} for k, v in eth_stall.most_common()},
        "events_count": dict(eth_ev_count), "dhcp_rc": dict(eth_dhcp_rc), "events_first": eth_events[:60],
        "max_tx_ms": max((s["tx_max_ms"] for s in eth_sums), default=None),
        "max_rx_ms": max((s["rx_max_ms"] for s in eth_sums), default=None),
        "counters_final": {k: last_sum[k] for k in ("got_ip_n", "downs", "renews", "renew_fail", "resets", "tx_fail", "rx_n")} if last_sum else None,
    }
    if not has_prefix:
        res["ext"] = {"counts": dict(ext_counts), "tx_stack_hwm_min": min((h for _, h, _ in ext_tx), default=None),
                      "tx_len": summ([float(l) for l, _, _ in ext_tx], 0)}
        res["initial_lines"] = initial_lines[:40]
    res["markers_top"] = dict(markers.most_common(45))
    # metrics that need `--setlog on` ([LOG] RX/TX/STAT lines); only emitted when the log has none, so
    # captures with [LOG] lines keep their result shape
    unavail: dict[str, str] = {}
    if not rx_counts["events"]:
        unavail["traffic_rf_neighbours_firmware_census"] = "no [LOG] RX lines (--setlog on missing): decoded frames, RSSI/SNR, originators, types and airtime unknown"
    if not res["stat"]["rows"]:
        unavail["stat_windows_heap_ringmax_drops"] = "no [LOG] STAT lines (--setlog on missing)"
    if not res["own_tx"].get("tx_lines"):
        unavail["own_tx_prio_wait_src"] = "no [LOG] TX lines (--setlog on missing); only RADIO_TX/TX_DONE markers counted"
    if unavail:
        res["unavailable"] = unavail
    return res


def main() -> None:
    norm, mdir, out = NORM, MASTER, OUT / "results.json"
    only = sys.argv[1:] or NODES
    master, dist = load_master(mdir) if mdir else ({}, {})
    result: dict[str, Any] = {"nodes": {}, "network": {}}
    if out.exists():
        try:
            result = json.loads(out.read_text())
        except json.JSONDecodeError:
            pass
    for n in only:
        print(f"[{n}] start", file=sys.stderr, flush=True)
        result["nodes"][n] = analyze(n, norm / f"{n}.log", master, dist)
        out.write_text(json.dumps(result, indent=1, default=str))
        print(f"[{n}] done", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
