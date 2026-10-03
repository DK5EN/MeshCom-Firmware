#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Regressionstests fuer docs/soak-20260928-verdict.md, Fund 8, T1 und T2.

T1 ``tools/nbrhopcheck.py``: ein Rufzeichen, das per Funk POS/HEY sendet oder weiterreicht, ist
   kein Server-Rufzeichen (auch wenn es in KNOWN_SERVER steht); nur per RX-UDP gesehene bleiben
   Server; veraltete Zeilen ("stale") sind kein Befund.
T2 ``tools/nbrlog.py``, ``tools/nbrsnap.py``: Deckung wie die Firmware (10-%-Anteil,
   90-min-Halbierung, 720-min-Fenster); NBR-Zeilen mitten in der Zeile (hinter einem
   CRC_PAYLOAD-Dump) werden gelesen; ``[NBR]|GW|...`` wird gezaehlt, nie ein Fehler.

Jeder Test schlaegt gegen die Staende vor dem Fix fehl: ``NBR_TOOLS_DIR=<Ordner mit den alten
Skripten>`` laedt sie stattdessen.

Usage::

    uv run test/test_nbrlog/test_nbr_soak_fixes.py
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import os
import re
import sys
import tempfile
from pathlib import Path

TOOLS_DIR = Path(
    os.environ.get("NBR_TOOLS_DIR") or Path(__file__).resolve().parents[2] / "tools"
)


CHECKS = 0  # Zaehler fuer die Schlusszeile "nbrlog: <name>: <M> checks"


def tally(cond):
    """Zaehlt jede tatsaechlich ausgefuehrte Pruefung, gibt die Bedingung unveraendert zurueck."""
    global CHECKS
    CHECKS += 1
    return cond


def load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS_DIR / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod  # @dataclass loest cls.__module__ ueber sys.modules auf
    spec.loader.exec_module(mod)
    return mod


def run_capturing(func, argv: list[str]) -> tuple[int, str]:
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            rc = func(argv)
    except SystemExit as exc:
        rc = 0 if exc.code is None else exc.code
    return (rc or 0), buf.getvalue()


class Log:
    """Baut einen Mitschnitt mit laufenden Host-Zeitstempeln (Sekundentakt)."""

    def __init__(self) -> None:
        self.n = 0
        self.lines: list[str] = []

    def add(self, text: str) -> None:
        self.n += 1
        self.lines.append(
            f"2026-09-27 17:{self.n // 60:02d}:{self.n % 60:02d}.000  {text}"
        )

    def write(self, tmp: Path, name: str = "x.log") -> Path:
        p = tmp / name
        p.write_text("\n".join(self.lines) + "\n", encoding="utf-8")
        return p


def check(label: str, got, want, failures: list[str]) -> None:
    if tally(got != want):
        failures.append(f"{label}: erwartet {want!r}, bekommen {got!r}")


# --------------------------------------------------------------------------
# T1 -- nbrhopcheck
# --------------------------------------------------------------------------

RF_POS = "[LOG] 062 @ xE9F11356 H02 S1 T0 M01 {path}>H@R14;17,46,6; HW:43 MOD:8/8 FCS:0E3A FW:35:t LH:AB RSSI:-116 SNR:-5"
UDP_TXT = "RX-UDP  039 : xDC40F157 H04 S1 T0 M01 {src}>*:{{DL2EMC -12}}hi HW:41 MOD:8/8 FCS:0A42 FW:35:k LH:A9"


def hop_log(
    *,
    rf_path: str | None,
    udp_src: str | None,
    rows: list[tuple[str, int, int, int, str]],
    edges: list[tuple[str, str]],
) -> Log:
    """Eine Funk-Nachbarin A (hop1), ``edges`` als EDGE-Zeilen, dann ein Schnappschuss.

    rows: (call, age, hearers, ..., verdict) -> ROW-Zeile (Flags 1, meshneed NA).
    """
    lg = Log()
    if rf_path:
        lg.add(RF_POS.format(path=rf_path))
    if udp_src:
        lg.add(UDP_TXT.format(src=udp_src))
    lg.add("[NBR]|ME|10|DL2JA-2|P|-80|3|5")
    for frm, to in edges:
        lg.add(f"[NBR]|EDGE|11|{frm}|{to}|P|0|2|NA")
    lg.add(f"[NBR]|SNAP|20|DK5EN-98|{len(rows) + 2}|128|4")
    lg.add("[NBR]|ROW|20|0|DK5EN-98|1|0|0|UNK|NA")
    lg.add("[NBR]|ROW|20|1|DL2JA-2|0|5|1|LEAF|NA")
    for i, (call, age, hearers, _x, verdict) in enumerate(rows, start=2):
        lg.add(f"[NBR]|ROW|20|{i}|{call}|0|{age}|{hearers}|{verdict}|NA")
    lg.add("[NBR]|ENDSNAP|20")
    return lg


def test_hopcheck(failures: list[str], tmp: Path) -> None:
    hc = load_module("nbrhopcheck")
    known = "DF4ND-99"
    assert known in hc.KNOWN_SERVER, "Testannahme: DF4ND-99 steht in KNOWN_SERVER"

    # (a) KNOWN_SERVER-Rufzeichen steht in einem per Funk gehoerten '@'-Pfad, Zeile ist hop 2 -> 0
    lg = hop_log(
        rf_path=f"{known},DL2JA-2",
        udp_src=None,
        rows=[(known, 5, 1, 0, "LEAF")],
        edges=[(known, "DL2JA-2")],
    )
    rc, out = run_capturing(hc.main, [str(lg.write(tmp, "a.log"))])
    check("T1a KNOWN_SERVER im RF-Pfad: Exit", rc, 0, failures)
    check(
        "T1a keine SERVER-Markierung",
        "SERVER" in out.replace("Server-Frames", ""),
        False,
        failures,
    )

    # (b) dasselbe Rufzeichen ohne Funkframe: bleibt Server -> Befund
    lg = hop_log(
        rf_path=None,
        udp_src=None,
        rows=[(known, 5, 1, 0, "LEAF")],
        edges=[(known, "DL2JA-2")],
    )
    rc, out = run_capturing(hc.main, [str(lg.write(tmp, "b.log"))])
    check("T1b KNOWN_SERVER ohne Funk: Exit", rc, 1, failures)
    check("T1b SERVER-Markierung", "<-- SERVER" in out, True, failures)

    # (c) Rufzeichen nur in RX-UDP (nicht in der Liste), Zeile hop 2 -> Befund
    lg = hop_log(
        rf_path=None,
        udp_src="DO3GB-1",
        rows=[("DO3GB-1", 5, 1, 0, "LEAF")],
        edges=[("DO3GB-1", "DL2JA-2")],
    )
    rc, out = run_capturing(hc.main, [str(lg.write(tmp, "c.log"))])
    check("T1c nur RX-UDP: Exit", rc, 1, failures)
    check("T1c SERVER-Markierung", "<-- SERVER" in out, True, failures)

    # (c2) RX-UDP UND per Funk als Relais gehoert -> Funkstation, kein Befund
    lg = hop_log(
        rf_path="DL2JA-2,DO3GB-1",
        udp_src="DO3GB-1",
        rows=[("DO3GB-1", 5, 1, 0, "LEAF")],
        edges=[("DO3GB-1", "DL2JA-2")],
    )
    rc, out = run_capturing(hc.main, [str(lg.write(tmp, "c2.log"))])
    check("T1c2 RX-UDP und RF-Relais: Exit", rc, 0, failures)

    # (d) veraltete Zeilen: Hoerer 0 / Alter >= 720 mit UNK, und Zeile, deren einziger Hoerer hop 2 ist
    lg = hop_log(
        rf_path=None,
        udp_src=None,
        rows=[
            ("DK4YU-77", 758, 0, 0, "UNK"),
            ("DF8RD-1", 638, 1, 0, "LEAF"),
            ("DL2JA-9", 5, 1, 0, "LEAF"),
        ],
        edges=[("DF8RD-1", "DB0ISM-1"), ("DL2JA-9", "DL2JA-2")],
    )
    rc, out = run_capturing(hc.main, [str(lg.write(tmp, "d.log"))])
    check("T1d stale: Exit", rc, 0, failures)
    check(
        "T1d stale DK4YU-77", bool(re.search(r"DK4YU-77\s+stale", out)), True, failures
    )
    check(
        "T1d stale DF8RD-1 (Hoerer nur Hop 2)",
        bool(re.search(r"DF8RD-1\s+stale", out)),
        True,
        failures,
    )
    check("T1d kein FAIL", "FAIL" in out, False, failures)
    check("T1d Zaehler stale 2", "stale  2" in out, True, failures)

    # (e) eine Zeile ohne jeden Hoerer, aber jung und nicht UNK, bleibt ein Befund
    lg = hop_log(
        rf_path=None, udp_src=None, rows=[("DL2JA-8", 5, 1, 0, "LEAF")], edges=[]
    )
    rc, out = run_capturing(hc.main, [str(lg.write(tmp, "e.log"))])
    check("T1e echter FAIL: Exit", rc, 1, failures)
    check("T1e FAIL-Text", "FAIL: kein direkter Hoerer" in out, True, failures)


# --------------------------------------------------------------------------
# T2 -- nbrlog / nbrsnap
# --------------------------------------------------------------------------


def share_log(
    edge_lines: list[str],
    snap_up: int,
    row_a: str,
    row_b: str,
    extra: list[str] | None = None,
) -> Log:
    """Eigene Station DK5EN-98 hoert A und B direkt; ``edge_lines`` sind fertige [NBR]-Zeilen."""
    lg = Log()
    lg.add("[NBR]|ME|50|A-1|P|-80|5|5")
    lg.add("[NBR]|ME|50|B-1|P|-80|5|5")
    for e in edge_lines:
        lg.add(e)
    for e in extra or []:
        lg.add(e)
    lg.add(f"[NBR]|SNAP|{snap_up}|DK5EN-98|4|128|6")
    lg.add(f"[NBR]|ROW|{snap_up}|0|DK5EN-98|1|0|0|UNK|NA")
    lg.add(f"[NBR]|ROW|{snap_up}|1|A-1|0|5|1|LEAF|{row_a}")
    lg.add(f"[NBR]|ROW|{snap_up}|2|B-1|0|5|1|LEAF|{row_b}")
    lg.add(f"[NBR]|ROW|{snap_up}|3|X-9|0|5|2|RED|NA")
    lg.add(f"[NBR]|ENDSNAP|{snap_up}")
    return lg


def nbrlog_result(nl, path: Path, *args: str) -> dict:
    """Ruft main() mit --json auf und liest das Ergebnis; ohne Flags: Firmware-Standard."""
    import json

    out_json = path.with_suffix(".json")
    rc, _ = run_capturing(nl.main, [str(path), "--json", str(out_json), *args])
    assert rc == 0
    return json.loads(out_json.read_text(encoding="utf-8"))


def nbrsnap_summary(ns, path: Path, *args: str) -> dict[str, tuple[int, int, int]]:
    """Zusammenfassung Geraetesicht: Rufzeichen -> (min, median, max) von #X."""
    rc, out = run_capturing(ns.main, [str(path), *args])
    assert rc == 0, out
    res: dict[str, tuple[int, int, int]] = {}
    for m in re.finditer(
        r"^\s+(\S+)\s+(\d+) / +(\d+) / +(\d+)  ueber", out, re.MULTILINE
    ):
        res[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    return res


def test_share_rule(failures: list[str], tmp: Path) -> None:
    nl, ns = load_module("nbrlog"), load_module("nbrsnap")
    # A-x cnt 37, B-x cnt 3, Firmware sagt A=MESH B=RED: B deckt x nicht (3 % < 10 %)
    lg = share_log(
        ["[NBR]|EDGE|60|X-9|A-1|P|0|37|NA", "[NBR]|EDGE|60|X-9|B-1|P|0|3|NA"],
        70,
        "MESH",
        "RED",
    )
    p = lg.write(tmp, "share.log")
    res = nbrlog_result(nl, p)
    urt = {r["rufzeichen"]: r for r in res["4_urteil"]["je_nachbar"]}
    check(
        "T2 share: nbrlog Abweichungen",
        [r["rufzeichen"] for r in res["4_urteil"]["abweichungen"]],
        [],
        failures,
    )
    check(
        "T2 share: nbrlog A exklusive Knoten",
        urt["A-1"]["exklusive_knoten"],
        ["X-9"],
        failures,
    )
    check(
        "T2 share: nbrlog B redundant",
        urt["B-1"]["eigenes_urteil"],
        "redundant",
        failures,
    )
    # mit --share-pct 0 (alte Ein-Treffer-Regel) faellt A wieder auf "redundant" -> Abweichung
    res0 = nbrlog_result(nl, p, "--share-pct", "0")
    check(
        "T2 share: --share-pct 0 wie alt (1 Abweichung, A)",
        [r["rufzeichen"] for r in res0["4_urteil"]["abweichungen"]],
        ["A-1"],
        failures,
    )
    summ = nbrsnap_summary(ns, p)
    check("T2 share: nbrsnap excl(A)", summ.get("A-1"), (1, 1, 1), failures)
    check("T2 share: nbrsnap excl(B)", summ.get("B-1"), (0, 0, 0), failures)
    summ0 = nbrsnap_summary(ns, p, "--share-pct", "0")
    check(
        "T2 share: nbrsnap --share-pct 0 excl(A)", summ0.get("A-1"), (0, 0, 0), failures
    )


def test_halving(failures: list[str], tmp: Path) -> None:
    nl, ns = load_module("nbrlog"), load_module("nbrsnap")
    # B-x cnt 3 bei up 100, A-x cnt 40 bei up 170. Bei up 180 halbiert die Firmware: 2 und 20;
    # 2 * 100 >= 10 * 20 -> B deckt x wieder, beide RED. Ohne Halbierung: 300 < 400 -> A exklusiv.
    lg = share_log(
        ["[NBR]|EDGE|100|X-9|B-1|P|0|3|NA", "[NBR]|EDGE|170|X-9|A-1|P|0|40|NA"],
        180,
        "RED",
        "RED",
    )
    p = lg.write(tmp, "halve.log")
    res = nbrlog_result(nl, p)
    check(
        "T2 halve: nbrlog Abweichungen",
        [r["rufzeichen"] for r in res["4_urteil"]["abweichungen"]],
        [],
        failures,
    )
    res_nh = nbrlog_result(nl, p, "--halve-min", "0")
    check(
        "T2 halve: --halve-min 0 gaebe A=MESH (Abweichung)",
        [r["rufzeichen"] for r in res_nh["4_urteil"]["abweichungen"]],
        ["A-1"],
        failures,
    )
    summ = nbrsnap_summary(ns, p)
    check("T2 halve: nbrsnap excl(A)", summ.get("A-1"), (0, 0, 0), failures)
    summ_nh = nbrsnap_summary(ns, p, "--halve-min", "0")
    check(
        "T2 halve: nbrsnap --halve-min 0 excl(A)",
        summ_nh.get("A-1"),
        (1, 1, 1),
        failures,
    )


def test_midline_and_gw(failures: list[str], tmp: Path) -> None:
    nl = load_module("nbrlog")
    lg = Log()
    lg.add(
        "[MC-DBG] CRC_PAYLOAD[116]: 21 87 E3 E1 1A 11 44 4B [NBR]|CHECK|36|15|30|0|0|0|0"
    )
    lg.add("\\x00\\x00[NBR]|ME|10|DK5EN-95|P|-85|1")  # Reconnect-Muell bleibt verworfen
    lg.add(
        "[NBR]|EDGE|10|A-1|B-1|P|-72[NBR]|ME|10|DK5EN-97|H|-90|1"
    )  # zwei Nutzzeilen verschweisst
    lg.add("[NBR]|SNAP|40|DK5EN-98|2|128|1")
    lg.add("[NBR]|ROW|40|0|DK5EN-98|1|0|0|UNK|NA")
    lg.add("[NBR]|ROW|40|1|DL2JA-2|1|5|1|LEAF|NA")  # ROW-Flag Bit 0 = Gateway
    lg.add("[NBR]|ENDSNAP|40")
    lg.add("[NBR]|GW|41|DL2JA-2|0|H")  # GW-Zeile widerspricht dem ROW-Flag: ROW gewinnt
    lg.add("[NBR]|GW|42|DL2JA-3|1|HG")
    lg.add("[NBR]|GW|43|DL2JA-4")  # zu kurz: nie ein Fehler
    lg.add("[NBR]|GW|abc|DL2JA-5|1|EXP")  # Minute kaputt: nie ein Fehler
    p = lg.write(tmp, "mid.log")
    state = nl.parse_files([p])
    check("T2 midline: CHECK gelesen", len(state.checks), 1, failures)
    reasons = dict(state.discard_reasons)
    check(
        "T2 midline: garbled_prefix nur der Muell",
        reasons.get("garbled_prefix"),
        1,
        failures,
    )
    check(
        "T2 midline: verschweisste Nutzzeilen weiter verworfen",
        reasons.get("glued_line"),
        1,
        failures,
    )
    check(
        "T2 midline: Anomalie gezaehlt",
        state.anomalies.get("nbr_mitten_in_zeile"),
        1,
        failures,
    )
    check("T2 GW: alle vier gezaehlt", len(state.gw_events), 4, failures)
    check("T2 GW: kein malformed", [k for k in reasons if "GW" in k], [], failures)
    gw = nl.analyze(state)["12_gateway"]
    check("T2 GW: ereignisse_gesamt", gw["ereignisse_gesamt"], 4, failures)
    rows = {r["rufzeichen"]: r for r in gw["je_rufzeichen"]}
    check(
        "T2 GW: ROW-Flag schlaegt GW-Zeile",
        (rows["DL2JA-2"]["gateway"], rows["DL2JA-2"]["quelle"]),
        (True, "ROW"),
        failures,
    )
    check(
        "T2 GW: Rueckfall auf GW-Zeile",
        (rows["DL2JA-3"]["gateway"], rows["DL2JA-3"]["quelle"]),
        (True, "GW"),
        failures,
    )


def main() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        for fn in (test_hopcheck, test_share_rule, test_halving, test_midline_and_gw):
            try:
                fn(failures, tmp)
            except Exception as exc:  # noqa: BLE001 -- ein kaputter Test darf die anderen nicht verdecken
                failures.append(f"{fn.__name__}: {type(exc).__name__}: {exc}")
    if failures:
        print(f"FEHLGESCHLAGEN ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print(
        "test_nbr_soak_fixes: alle Pruefungen bestanden (nbrhopcheck T1, nbrlog/nbrsnap T2)."
    )
    print(f"nbrlog: {Path(__file__).stem}: {CHECKS} checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
