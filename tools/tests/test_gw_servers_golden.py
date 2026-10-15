"""Legacy-vs-new diff for the central gateway server table (docs/gw-servers-unification.md 3.5).

test/golden/gw_servers_legacy.json was produced by EXECUTING the pre-refactor chains
(tools/gw_legacy_capture.py) and is never regenerated from new code. This test compiles
src/gw_servers.cpp with tools/gw_servers_dump.cpp, runs the same grid through
gwResolveNode(), and forces every difference to be NAMED in EXPECTED_DELTAS:

  (a) a differing field matched by no rule        -> UNEXPLAINED, test fails
  (b) a rule that matches no differing field      -> stale, test fails
  (c) a rule whose stated new value is not the
      actual new value                            -> test fails

New values are stated independently of the C++ in NEW_TABLE (doc sections 1.2-1.4, 3.3, 5.1).
Environment: GW_SRC (path of gw_servers.cpp, mutation checks), GW_DIFF_REPORT (write the
grouped markdown report there).
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

import pytest

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "test" / "golden" / "gw_servers_legacy.json"
DUMP_CPP = ROOT / "tools" / "gw_servers_dump.cpp"
GW_SRC = Path(os.environ.get("GW_SRC", ROOT / "src" / "gw_servers.cpp"))

Row = dict[str, Any]
FIELDS = ("srv", "ntp", "path")

# ---------------------------------------------------------------------------
# Independent statement of the NEW behaviour: (server, ntp) per code and network.
# esp32 uses host names where the table has one; nRF52 uses the literals (no resolver).
# ---------------------------------------------------------------------------
_OE_I = {"esp32": ("meshcom.oevsv.at", "pool.ntp.org"), "nrf": ("89.185.97.38", "162.159.200.1")}
_OE_H = {"esp32": ("44.143.8.143", "44.143.0.9"), "nrf": ("44.143.8.143", "44.143.0.9")}
_DL_I = {
    "esp32": ("meshcom.hamnet.network", "pool.ntp.org"),
    "nrf": ("192.68.17.26", "162.159.200.1"),
}
_HC_H = {"esp32": ("44.148.230.197", "44.148.224.123"), "nrf": ("44.148.230.197", "44.148.224.123")}
_IT_I = {
    "esp32": ("meshcom.dig-italia.it", "pool.ntp.org"),
    "nrf": ("145.239.75.155", "162.159.200.1"),
}
_HB_I = {"esp32": ("meshcom.emcomm.ch", "pool.ntp.org"), "nrf": ("157.143.4.44", "162.159.200.1")}
_HB_H = {"esp32": ("44.142.42.59", "44.142.163.2"), "nrf": ("44.142.42.59", "44.142.163.2")}
NEW_TABLE: dict[str, dict[str, dict[str, tuple[str, str]]]] = {
    "OE": {"inet": _OE_I, "hamnet": _OE_H},
    "DL": {"inet": _DL_I, "hamnet": _HC_H},  # DL on the Hamnet goes via HC (D9)
    "IT": {"inet": _IT_I, "hamnet": _IT_I},  # no Hamnet server: Internet used, path inet
    "HC": {"inet": _DL_I, "hamnet": _HC_H},  # HC Internet goes via DL
    "HB": {"inet": _HB_I, "hamnet": _HB_H},
}


def plat_kind(platform: str) -> str:
    return "esp32" if platform == "esp32" else "nrf"


def legacy_net(r: Row) -> str:
    """Net decision of the legacy chains (doc 1.1)."""
    if r["platform"] == "nrf_static":
        return "hamnet" if r["own_ip"].startswith("44") or r["hamnet_only"] != 0 else "inet"
    return "hamnet" if r["hamnet_only"] == 1 else "inet"


def new_net(r: Row) -> str:
    """gwSelectNet: 0 and 3 Internet, 1 Hamnet, anything else auto by own address.

    nrf_static (startFIXUDP) maps mode 0 to the legacy static rule first (44.x -> Hamnet,
    else Internet) and passes every other value on unchanged (doc section 4).
    """
    m = r["hamnet_only"]
    if r["platform"] == "nrf_static" and m == 0:
        return "hamnet" if r["own_ip"].startswith("44.") else "inet"
    if m in (0, 3):
        return "inet"
    if m == 1:
        return "hamnet"
    return "hamnet" if r["own_ip"].startswith("44.") else "inet"


def expected_new(r: Row) -> dict[str, str]:
    code = r["code"] if r["code"] in NEW_TABLE else "OE"
    net = new_net(r)
    srv, ntp = NEW_TABLE[code][net][plat_kind(r["platform"])]
    own = r["own_ntp"] if r["platform"] == "esp32" else ""
    if len(own) >= 7:
        ntp = own
    path = "inet" if code == "IT" else net
    return {"srv": srv, "ntp": ntp, "path": path}


# ---------------------------------------------------------------------------
# EXPECTED_DELTAS: every documented difference, by name.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Delta:
    name: str
    doc: str
    fields: tuple[str, ...]  # fields this rule explains
    when: Callable[[Row, Row], bool]  # (legacy row, new row)
    old_new: str  # human description old -> new


def _net_changed(r: Row) -> bool:
    return legacy_net(r) != new_net(r)


EXPECTED_DELTAS: list[Delta] = [
    Delta(
        "D2/D3 net: mode 2 auto (esp32, nrf_dyn)",
        "doc 1.1, 4",
        FIELDS,
        lambda r, n: r["platform"] != "nrf_static" and r["hamnet_only"] == 2 and _net_changed(r),
        "mode 2 with a 44er address: Internet -> Hamnet (legacy: only mode 1 was Hamnet)",
    ),
    Delta(
        "D2/D3 net: mode 7 auto (esp32, nrf_dyn)",
        "doc 1.1, 4",
        FIELDS,
        lambda r, n: r["platform"] != "nrf_static" and r["hamnet_only"] == 7 and _net_changed(r),
        "mode 7 (any other value) with a 44er address: Internet -> Hamnet",
    ),
    Delta(
        "D3 static: mode 2 = auto",
        "doc 4",
        FIELDS,
        lambda r, n: r["platform"] == "nrf_static" and r["hamnet_only"] == 2 and _net_changed(r),
        "static nRF52, mode 2 with a non-44er address: Hamnet (legacy: any non-zero) -> Internet",
    ),
    Delta(
        "D3 static: mode 7 = auto",
        "doc 4",
        FIELDS,
        lambda r, n: r["platform"] == "nrf_static" and r["hamnet_only"] == 7 and _net_changed(r),
        "static nRF52, mode 7 (any other value) with a non-44er address: Hamnet -> Internet",
    ),
    Delta(
        "D1 nRF52 static gains HC/HB",
        "doc 2 D1, 5.1",
        FIELDS,
        lambda r, n: (
            r["platform"] == "nrf_static" and r["code"] in ("HC", "HB") and not _net_changed(r)
        ),
        "HC/HB on static nRF52: OE server -> the row's own server and NTP",
    ),
    Delta(
        "D9b ESP32 DL on the Hamnet: DNS name -> literal",
        "doc 2 D9, 5.1",
        ("srv",),
        lambda r, n: (
            r["platform"] == "esp32"
            and r["code"] == "DL"
            and new_net(r) == "hamnet"
            and not _net_changed(r)
        ),
        "meshcom.hamnet.cloud -> 44.148.230.197 (same host: dig +short meshcom.hamnet.cloud = 44.148.230.197; DL on the Hamnet is now served via HC)",
    ),
    Delta(
        "D6 ESP32 IT forced-Hamnet NTP is the Internet NTP",
        "doc 2 D6, 3.3, 5.1",
        ("ntp",),
        lambda r, n: (
            r["platform"] == "esp32"
            and r["code"] == "IT"
            and new_net(r) == "hamnet"
            and not _net_changed(r)
        ),
        "ESP32, IT on the Hamnet: Hamnet NTP by octet -> Internet NTP (pool.ntp.org form on ESP32; 162.159.200.1 literal on nRF52)",
    ),
    Delta(
        "D4 ESP32 Hamnet NTP per server, not per own octet",
        "doc 2 D4, 3.3, 5.1",
        ("ntp",),
        lambda r, n: (
            r["platform"] == "esp32"
            and r["code"] != "IT"
            and new_net(r) == "hamnet"
            and not _net_changed(r)
        ),
        "ESP32 on the Hamnet: NTP by own second octet -> NTP of the server's Hamnet side",
    ),
]


def _fmt(v: dict[str, str]) -> str:
    return f"{v['srv']} / {v['ntp']} / {v['path']}"


def _key(r: Row) -> str:
    return f"{r['platform']} {r['code'] or '-'} ip={r['own_ip']} mode={r['hamnet_only']} ntp={r['own_ntp'] or '-'}"


def _dump_line(r: Row) -> str:
    ntp = r["own_ntp"] if r["own_ntp"] not in ("", "n/a") else "-"
    return f"{r['platform']} {r['code'] or '-'} {r['own_ip']} {r['hamnet_only']} {ntp}"


@pytest.fixture(scope="module")
def dumper(tmp_path_factory: pytest.TempPathFactory) -> Path:
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        pytest.skip("no host C++ compiler")
    out = tmp_path_factory.mktemp("gwdump") / "gw_servers_dump"
    cmd = [cxx, "-std=gnu++17", "-I", str(ROOT / "src"), str(GW_SRC), str(DUMP_CPP), "-o", str(out)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 0, res.stderr
    return out


def _run(dumper: Path, lines: list[str]) -> list[Row]:
    res = subprocess.run(
        [str(dumper)], input="\n".join(lines) + "\n", capture_output=True, text=True
    )
    assert res.returncode == 0, res.stderr
    rows = [json.loads(x) for x in res.stdout.splitlines()]
    assert len(rows) == len(lines)
    return rows


@dataclass
class Analysis:
    pairs: list[tuple[Row, Row]]
    matched: dict[str, list[tuple[Row, Row, str]]]  # rule -> (legacy, new, field)
    unexplained: dict[tuple[str, ...], list[tuple[Row, Row]]]  # differing fields -> rows
    wrong_value: list[str]
    differing_rows: int


def analyse(dumper: Path) -> Analysis:
    legacy = json.loads(GOLDEN.read_text())["rows"]
    assert len(legacy) == 700
    new = _run(dumper, [_dump_line(r) for r in legacy])
    matched: dict[str, list[tuple[Row, Row, str]]] = defaultdict(list)
    unexplained: dict[tuple[str, ...], list[tuple[Row, Row]]] = defaultdict(list)
    wrong: list[str] = []
    pairs = list(zip(legacy, new))
    diffrows = 0
    for old, nw in pairs:
        diff = [f for f in FIELDS if old[f] != nw[f]]
        if not diff:
            continue
        diffrows += 1
        exp = expected_new(old)
        left: list[str] = []
        for f in diff:
            rules = [d for d in EXPECTED_DELTAS if f in d.fields and d.when(old, nw)]
            if not rules:
                left.append(f)
                continue
            d = rules[0]  # first match wins: net decision, then D1, D6, D4
            matched[d.name].append((old, nw, f))
            # (c) the stated new value must be the actual new value
            if exp[f] != nw[f]:
                wrong.append(
                    f"{_key(old)} field {f}: rule {d.name!r} states {exp[f]!r}, actual {nw[f]!r}"
                )
        if left:
            unexplained[tuple(left)].append((old, nw))
    return Analysis(pairs, matched, unexplained, wrong, diffrows)


@pytest.fixture(scope="module")
def analysis(dumper: Path) -> Analysis:
    a = analyse(dumper)
    report = os.environ.get("GW_DIFF_REPORT")
    if report:
        Path(report).write_text(render_report(a))
    return a


def render_report(a: Analysis) -> str:
    out = ["# gw_servers: legacy vs new, grouped by delta rule", ""]
    out.append(
        f"Grid rows: {len(a.pairs)}; rows that differ in at least one field: {a.differing_rows}."
    )
    out.append("")
    out.append(
        "| Rule | Doc | Fields x rows | Old -> new | Example (legacy -> new: srv / ntp / path) |"
    )
    out.append(
        "| ---- | --- | ------------- | ---------- | ----------------------------------------- |"
    )
    for d in EXPECTED_DELTAS:
        hits = a.matched.get(d.name, [])
        if hits:
            o, n, _f = hits[0]
            ex = f"{_key(o)}: {_fmt(o)} -> {_fmt(n)}"
        else:
            ex = "(no hit: STALE)"
        out.append(f"| {d.name} | {d.doc} | {len(hits)} | {d.old_new} | {ex} |")
    out.append("")
    out.extend(render_matrix(a))
    out.append("## UNEXPLAINED (differences no rule covers)")
    out.append("")
    if not a.unexplained:
        out.append("none")
    for fields, rows in sorted(a.unexplained.items()):
        out.append(f"### fields {list(fields)}: {len(rows)} rows")
        for o, n in rows[:3]:
            out.append(f"- {_key(o)}: {_fmt(o)} -> {_fmt(n)}")
        out.append("")
    if a.wrong_value:
        out.append("## Rule matched but stated new value differs from actual")
        out.extend(f"- {w}" for w in a.wrong_value)
    return "\n".join(out) + "\n"


def render_matrix(a: Analysis) -> list[str]:
    """Old vs new per platform; Internet = 10.0.0.5 mode 0, Hamnet = 44.143.1.5 mode 1, no own NTP."""
    idx = {
        (o["platform"], o["code"], o["own_ip"], o["hamnet_only"], o["own_ntp"] in ("", "n/a")): (
            o,
            n,
        )
        for o, n in a.pairs
    }

    def cell(plat: str, code: str, ip: str, mode: int) -> str:
        o, n = idx[(plat, code, ip, mode, True)]
        if (o["srv"], o["ntp"], o["path"]) == (n["srv"], n["ntp"], n["path"]):
            return "="
        return f"{o['srv']} / {o['ntp']} -> {n['srv']} / {n['ntp']}"

    out = ["## Old vs new matrix", ""]
    out.append(
        "Internet column: own 10.0.0.5, mode 0. Hamnet column: own 44.143.1.5, mode 1. No own NTP. `=` is unchanged."
    )
    out.append("")
    for plat in ("esp32", "nrf_dyn", "nrf_static"):
        out += [f"### {plat}", "", "| Code | Internet | Hamnet |", "| ---- | -------- | ------ |"]
        for code in ("OE", "DL", "IT", "HC", "HB"):
            i = cell(plat, code, "10.0.0.5", 0)
            h = cell(plat, code, "44.143.1.5", 1)
            out.append(f"| {code} | {i} | {h} |")
        out.append("")
    out += [
        "### Results that depend on own address, mode value or own NTP",
        "",
        "- Mode 2 and mode 7 (any value other than 0/1): old esp32 and nrf_dyn always Internet; new auto, Hamnet when the own address is 44.x (Internet otherwise).",
        "- nrf_static: mode 0 keeps the legacy rule (44.x -> Hamnet, else Internet); mode 1 Hamnet, 3 Internet, 2/7 auto (legacy: any non-zero was Hamnet). HC/HB gain their own rows (D1).",
        "- esp32 Hamnet NTP: old by own second octet (143/148/other); new by server (matrix shows the 44.143 case only).",
        "- Own NTP (esp32 only in the grid): wins over the table in old and new; nRF52 now honours an IPv4 literal, host names are ignored (D5, outside the grid).",
        "",
    ]
    return out


def test_no_unexplained_difference(analysis: Analysis) -> None:
    msgs = []
    for fields, rows in sorted(analysis.unexplained.items()):
        ex = "; ".join(f"{_key(o)}: {_fmt(o)} -> {_fmt(n)}" for o, n in rows[:3])
        msgs.append(f"{len(rows)} rows differ in {list(fields)} with no named delta, e.g. {ex}")
    assert not msgs, "UNEXPLAINED: " + " | ".join(msgs)


def test_no_stale_rule(analysis: Analysis) -> None:
    stale = [d.name for d in EXPECTED_DELTAS if not analysis.matched.get(d.name)]
    assert not stale, f"rules that match no differing row: {stale}"


def test_rule_new_values_are_actual(analysis: Analysis) -> None:
    assert not analysis.wrong_value, "\n".join(analysis.wrong_value)


def test_unchanged_rows_stay_unchanged(analysis: Analysis) -> None:
    """Internet NTP of ESP32 (pool.ntp.org) and nRF52 (162.159.200.1) must not move."""
    for old, nw in analysis.pairs:
        if old["platform"] == "esp32" and old["ntp"] == "pool.ntp.org" and new_net(old) == "inet":
            assert nw["ntp"] == "pool.ntp.org", _key(old)
        if old["platform"] != "esp32" and old["ntp"] == "162.159.200.1" and new_net(old) == "inet":
            assert nw["ntp"] == "162.159.200.1", _key(old)


# ---------------------------------------------------------------------------
# D5: own NTP on nRF52 is outside the golden grid (legacy never read it): extra rows.
# ---------------------------------------------------------------------------
def test_d5_nrf_own_ntp_literal_extra_rows(dumper: Path, analysis: Analysis) -> None:
    legacy = {_key(o): o for o, _ in analysis.pairs}
    cases = []
    for plat in ("nrf_dyn", "nrf_static"):
        for code in ("OE", "DL", "HB"):
            for ip, mode in (("10.0.0.5", 0), ("44.143.1.5", 1)):
                cases.append((plat, code, ip, mode))
    lines = [f"{p}_own {c} {ip} {m} 9.9.9.9" for p, c, ip, m in cases]
    lines += [f"{p}_own {c} {ip} {m} ntp.example.org" for p, c, ip, m in cases]
    lines += [
        f"{p}_own {c} {ip} {m} 1.2.3.456" for p, c, ip, m in cases
    ]  # >= 7 chars, not an IPv4 literal
    new = _run(dumper, lines)
    for line, n in zip(lines, new):
        plat = line.split()[0].removesuffix("_own")
        base = legacy[f"{plat} {n['code']} ip={n['own_ip']} mode={n['hamnet_only']} ntp=n/a"]
        if n["own_ntp"] == "9.9.9.9":
            assert n["ntp"] == "9.9.9.9", line  # D5: used when it is an IPv4 literal
            assert base["ntp"] != "9.9.9.9", line  # legacy ignored it
            assert n["ownntp_ignored"] is False
        else:
            assert n["ownntp_ignored"] is True, line  # host name / junk: ignored without resolver
            assert n["ntp"] == expected_new({**base, "own_ntp": ""})["ntp"], line


# ---------------------------------------------------------------------------
# Mode 3 (explicit Internet) is outside the legacy grid: expect Internet on all platforms,
# also for a 44er static address (finding D2).
# ---------------------------------------------------------------------------
def test_mode3_is_internet_everywhere(dumper: Path) -> None:
    lines = [
        f"{p} {c} {ip} 3 -"
        for p in ("esp32", "nrf_dyn", "nrf_static")
        for c in ("OE", "DL", "IT", "HC", "HB")
        for ip in ("10.0.0.5", "44.143.1.5")
    ]
    for line, n in zip(lines, _run(dumper, lines)):
        plat, code, ip = line.split()[:3]
        exp = NEW_TABLE[code]["inet"][plat_kind(plat)]
        assert (n["srv"], n["ntp"]) == exp, line
        assert n["path"] == "inet", line
