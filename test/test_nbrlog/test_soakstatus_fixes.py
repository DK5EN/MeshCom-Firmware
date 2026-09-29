#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Regressionstests fuer docs/soak-20260928-verdict.md, Fund 8, T3 (``tools/soakstatus.py``).

- Ack-Pfad und Latenz: die FRUEHESTE Zeile aus ``[UDP-MSGID] ack_msg_id:<orig> ACK...02`` (Server)
  und ``[ACK-MSGID] ack_msg_id:<orig>`` (LoRa) gewinnt; die ``[RETX]``-Marker sind nur Rueckfall.
  Fixtures haben die Form von DM #2 (Server-Ack vor LoRa-Ack) und DM #3 (LoRa-Ack vor Server-Ack)
  aus dem Soak vom 2026-09-27.
- Kopien: echte Empfaenge (RX-UDP + RF ``[LOG] NNN``), nicht MH-LoRa + RX-LoRa2 derselben Zeile.
- Lebendigkeit: Abstand ``--since`` -> erste Zeile und letzte Zeile -> ``--until`` zaehlt mit.
- Fehlender Mitschnitt: ein Knoten ohne Zeile im Fenster ist ein eigenes FAIL.
- Ein-Knoten-Lauf: ``--nodes DK5EN-98`` / ``--dk1-dir none`` ohne Schein-FAIL fuer DK5EN-1.

Jeder Test schlaegt gegen den Stand vor dem Fix fehl: ``NBR_TOOLS_DIR=<Ordner mit dem alten
soakstatus.py und nbrlog.py>`` laedt diesen stattdessen.

Usage::

    uv run test/test_nbrlog/test_soakstatus_fixes.py
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import os
import sys
import tempfile
from datetime import datetime, timedelta
from pathlib import Path

TOOLS_DIR = Path(
    os.environ.get("NBR_TOOLS_DIR") or Path(__file__).resolve().parents[2] / "tools"
)


def load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS_DIR / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def check(label: str, got, want, failures: list[str]) -> None:
    if got != want:
        failures.append(f"{label}: erwartet {want!r}, bekommen {got!r}")


TXT = "soak 20260927 #{n} {frm}>{to}{{{nnn}"
HDR = "H04 S0 T0 M01"


def frame(
    kind: str, mid: str, frm: str, to: str, n: int, nnn: int, dev: str = "17:54:27"
) -> str:
    """Eine Frame-Zeile wie printBuffer_aprs sie schreibt (Text der Test-DM)."""
    return f"{dev} {kind:8} 069 : x{mid} {HDR} {frm}>{to}:{TXT.format(n=n, frm=frm, to=to, nnn=nnn)} HW:43 MOD:8/8 FCS:10C3 FW:35:t LH:AB"


def rf_log(mid: str, frm: str, to: str, n: int, nnn: int) -> str:
    return (
        f"17:54:33 [LOG] 069 : x{mid} {HDR} {frm}>{to}:{TXT.format(n=n, frm=frm, to=to, nnn=nnn)} "
        "HW:43 MOD:8/8 FCS:10C3 FW:35:t LH:AB RSSI:-71 SNR:7 DUP:n OWN:- t=1238546"
    )


def to_lines(rows: list[tuple[str, str]]) -> list[tuple[datetime, str]]:
    return [(datetime.strptime(t, "%Y-%m-%d %H:%M:%S.%f"), rest) for t, rest in rows]


def dm(
    ss,
    frm: str,
    to: str,
    n: int,
    sent: str,
    lines_by_node: dict[str, list[tuple[datetime, str]]],
):
    send = ss.SoakSend(
        n, frm, to, datetime.strptime(sent, "%Y-%m-%d %H:%M:%S"), 200, "ok"
    )
    return ss.correlate_dm(send, "20260927", lines_by_node, timedelta(minutes=20))


def test_ack_path(failures: list[str]) -> None:
    ss = load_module("soakstatus")

    # DM #2-Form: DK5EN-98 sendet, das Server-Ack (4,1 s) kommt VOR dem LoRa-Ack (9,9 s). Es gibt
    # KEINE [RETX]-Marker -- die Firmware druckt sie nicht, wenn das Ack dem ersten LoRa-TX zuvorkommt.
    mid = "1AE1E2C4"
    d98 = to_lines(
        [
            (
                "2026-09-27 17:54:28.100",
                frame("NEW-TXT", mid, "DK5EN-98", "DK5EN-1", 2, 708),
            ),
            (
                "2026-09-27 17:54:28.114",
                frame("TX-UDP", mid, "DK5EN-98", "DK5EN-1", 2, 708),
            ),
            ("2026-09-27 17:54:32.229", f"[UDP-MSGID] ack_msg_id:{mid} ACK...02"),
            (
                "2026-09-27 17:54:33.237",
                frame("TX-LoRa", mid, "DK5EN-98", "DK5EN-1", 2, 708),
            ),
            ("2026-09-27 17:54:37.957", f"17:54:36[ACK-MSGID] ack_msg_id:{mid}"),
        ]
    )
    d1 = to_lines(
        [
            (
                "2026-09-27 17:54:28.729",
                frame("RX-UDP", mid, "DK5EN-98", "DK5EN-1", 2, 708),
            ),
            ("2026-09-27 17:54:34.754", rf_log(mid, "DK5EN-98", "DK5EN-1", 2, 708)),
            (
                "2026-09-27 17:54:34.754",
                frame("MH-LoRa", mid, "DK5EN-98", "DK5EN-1", 2, 708),
            ),
            (
                "2026-09-27 17:54:34.754",
                frame("RX-LoRa2", mid, "DK5EN-98", "DK5EN-1", 2, 708),
            ),
        ]
    )
    r2 = dm(
        ss,
        "DK5EN-98",
        "DK5EN-1",
        2,
        "2026-09-27 17:54:28",
        {"DK5EN-98": d98, "DK5EN-1": d1},
    )
    check("T3 DM#2 acked", r2.acked, True, failures)
    check("T3 DM#2 Pfad = server/UDP", r2.ack_path, "server/UDP", failures)
    check("T3 DM#2 Latenz ~4.1 s", round(r2.ack_latency_s or -1, 1), 4.1, failures)
    # Kopien: 1 RX-UDP + 1 RF-Empfang (3 Zeilen: [LOG], MH-LoRa, RX-LoRa2) = 2, nicht 4
    check(
        "T3 DM#2 Kopien (Dreifach-Zeile = 1 Empfang)", r2.receiver_copies, 2, failures
    )

    # DM #3-Form: DK5EN-1 sendet, das LoRa-Ack (1,0 s) kommt VOR dem Server-Ack (4,0 s); der [RETX]-
    # Marker "server ACK" steht spaet im Log und darf den Pfad nicht bestimmen.
    mid = "EA25A327"
    d1 = to_lines(
        [
            (
                "2026-09-27 18:09:25.930",
                frame("NEW-TXT", mid, "DK5EN-1", "DK5EN-98", 3, 807, "17:09:24"),
            ),
            (
                "2026-09-27 18:09:25.930",
                frame("TX-UDP", mid, "DK5EN-1", "DK5EN-98", 3, 807, "17:09:24"),
            ),
            ("2026-09-27 18:09:26.935", f"17:09:25[ACK-MSGID] ack_msg_id:{mid}"),
            (
                "2026-09-27 18:09:29.950",
                f"[RETX] server ACK for retid:3 stop retransmit msg-id:{mid}",
            ),
            ("2026-09-27 18:09:29.950", f"[UDP-MSGID] ack_msg_id:{mid} ACK...02"),
        ]
    )
    r3 = dm(
        ss,
        "DK5EN-1",
        "DK5EN-98",
        3,
        "2026-09-27 18:09:25",
        {"DK5EN-1": d1, "DK5EN-98": []},
    )
    check("T3 DM#3 Pfad = LoRa", r3.ack_path, "LoRa", failures)
    check("T3 DM#3 Latenz ~1.0 s", round(r3.ack_latency_s or -1, 1), 1.0, failures)

    # Rueckfall: nur [RETX]-Marker vorhanden (aeltere Logs) -> weiter erkannt
    d1 = to_lines(
        [
            (
                "2026-09-27 18:09:25.930",
                frame("NEW-TXT", mid, "DK5EN-1", "DK5EN-98", 3, 807, "17:09:24"),
            ),
            (
                "2026-09-27 18:09:27.500",
                f"[RETX] DM-ACK for retid:3 stop retransmit msg-id:{mid}",
            ),
        ]
    )
    r3b = dm(
        ss,
        "DK5EN-1",
        "DK5EN-98",
        3,
        "2026-09-27 18:09:25",
        {"DK5EN-1": d1, "DK5EN-98": []},
    )
    check(
        "T3 RETX-Rueckfall: acked, LoRa",
        (r3b.acked, r3b.ack_path),
        (True, "LoRa"),
        failures,
    )

    # Ack einer ANDEREN msg-id zaehlt nicht
    d1 = to_lines(
        [
            (
                "2026-09-27 18:09:25.930",
                frame("NEW-TXT", mid, "DK5EN-1", "DK5EN-98", 3, 807, "17:09:24"),
            ),
            ("2026-09-27 18:09:26.935", "17:09:25[ACK-MSGID] ack_msg_id:DEADBEEF"),
        ]
    )
    r3c = dm(
        ss,
        "DK5EN-1",
        "DK5EN-98",
        3,
        "2026-09-27 18:09:25",
        {"DK5EN-1": d1, "DK5EN-98": []},
    )
    check("T3 fremde msg-id: nicht acked", r3c.acked, False, failures)


def test_liveness(failures: list[str]) -> None:
    ss = load_module("soakstatus")
    since, until = datetime(2026, 9, 27, 17, 39), datetime(2026, 9, 27, 18, 53)

    def metrics(times: list[str]) -> object:
        lines = [
            (datetime.strptime(t, "%Y-%m-%d %H:%M:%S.%f"), "some line") for t in times
        ]
        sl = ss.SlicedLog(lines=lines, files=["x"] if lines else [])
        return ss.compute_node_metrics("N", sl, {}, since, until)

    # Zeilen jede Minute, dann tot ab 17:50: keine Luecke ZWISCHEN Zeilen, aber der Schwanz bis 18:53
    live = [f"2026-09-27 17:{m:02d}:00.000" for m in range(39, 51)]
    m = metrics(live)
    check("T3 toter Schwanz: alive", m.alive, False, failures)
    check("T3 toter Schwanz: Schwanz > 3000 s", m.tail_gap_s > 3000, True, failures)
    check(
        "T3 toter Schwanz: keine Luecke zwischen Zeilen",
        m.gaps_over_bluf_threshold,
        [],
        failures,
    )

    # Mitschnitt beginnt erst 40 min nach --since
    late = [f"2026-09-27 18:{m:02d}:00.000" for m in range(20, 53)] + [
        "2026-09-27 18:52:59.000"
    ]
    m = metrics(late)
    check("T3 spaeter Beginn: alive", m.alive, False, failures)
    check("T3 spaeter Beginn: Vorlauf ~ 2460 s", round(m.lead_gap_s), 2460, failures)

    # durchgehend bis zum Ende: alive
    full = [
        f"2026-09-27 {h}:{mm:02d}:00.000"
        for h, rng in (("17", range(39, 60)), ("18", range(53)))
        for mm in rng
    ]
    m = metrics(full + ["2026-09-27 18:52:59.000"])
    check("T3 vollstaendig: alive", m.alive, True, failures)

    # keine einzige Zeile: capture_missing, nicht alive
    m = metrics([])
    check(
        "T3 leer: capture_missing",
        (m.capture_missing, m.alive),
        (True, False),
        failures,
    )


def _write_node(dir_: Path, day: str, lines: list[str]) -> None:
    dir_.mkdir(parents=True, exist_ok=True)
    (dir_ / f"{day}.log").write_text("\n".join(lines) + "\n", encoding="utf-8")


STAT = (
    "{t}  16:38:54 [LOG] STAT util=5 rx=9000 tx=6000 newid=8 dup=0 err=0 txn=9 txfail=0 ringmax=1/20 "
    "drop=0/0/0/0/0 mh=1 heap=140000 trk=240/1 fw=35t/20260927 up=300 t=300003"
)


def run_main(ss, argv: list[str]) -> tuple[int, str]:
    buf, err = io.StringIO(), io.StringIO()
    try:
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(err):
            rc = ss.main(argv)
    except SystemExit as exc:
        rc = (
            2 if exc.code is None else int(exc.code) if isinstance(exc.code, int) else 2
        )
        return rc, buf.getvalue() + err.getvalue()
    return rc, buf.getvalue()


def test_missing_and_single_node(failures: list[str], tmp: Path) -> None:
    ss = load_module("soakstatus")
    d98 = tmp / "dk5en-98-local"
    stamps = [f"2026-09-27 17:{m:02d}:30.000" for m in range(39, 60)] + [
        f"2026-09-27 18:{m:02d}:30.000" for m in range(53)
    ]
    _write_node(d98, "2026-09-27", [STAT.format(t=t) for t in stamps])
    empty = tmp / "dk5en-1-empty"
    empty.mkdir()
    base = [
        "--since",
        "2026-09-27 17:39",
        "--until",
        "2026-09-27 18:53",
        "--soak-log",
        str(tmp / "none.log"),
    ]

    # Zwei-Knoten-Standard, DK5EN-1 ohne Zeile: EIGENES FAIL "capture missing", sonst keine Schein-FAILs
    rc, out = run_main(ss, [*base, "--dk1-dir", str(empty), "--dk98-dir", str(d98)])
    check("T3 fehlender Mitschnitt: Exit", rc, 1, failures)
    fails = [ln for ln in out.splitlines() if ln.startswith("- [FAIL]")]
    check("T3 fehlender Mitschnitt: genau ein FAIL", len(fails), 1, failures)
    check(
        "T3 fehlender Mitschnitt: Text",
        bool(fails) and "capture missing: DK5EN-1" in fails[0],
        True,
        failures,
    )

    # Ein-Knoten-Lauf per --nodes und per --dk1-dir none: kein DK5EN-1-FAIL, Exit 0
    for label, extra in (
        (
            "--nodes",
            ["--nodes", "DK5EN-98", "--dk1-dir", str(empty), "--dk98-dir", str(d98)],
        ),
        ("--dk1-dir none", ["--dk1-dir", "none", "--dk98-dir", str(d98)]),
    ):
        rc, out = run_main(ss, [*base, *extra])
        check(f"T3 Ein-Knoten ({label}): Exit", rc, 0, failures)
        check(f"T3 Ein-Knoten ({label}): kein FAIL", "[FAIL]" in out, False, failures)
        check(
            f"T3 Ein-Knoten ({label}): kein DK5EN-1 im Bericht",
            "## DK5EN-1" in out,
            False,
            failures,
        )
        check(
            f"T3 Ein-Knoten ({label}): DK5EN-98 im Bericht",
            "## DK5EN-98" in out,
            True,
            failures,
        )

    # toter Schwanz im Zwei-Knoten-Lauf: DK5EN-1 endet 17:50 -> FAIL "alive", trotz lueckenloser Zeilen davor
    d1 = tmp / "dk5en-1-dead"
    _write_node(
        d1,
        "2026-09-27",
        [STAT.format(t=f"2026-09-27 17:{m:02d}:30.000") for m in range(39, 51)],
    )
    rc, out = run_main(ss, [*base, "--dk1-dir", str(d1), "--dk98-dir", str(d98)])
    check("T3 toter Schwanz (main): Exit", rc, 1, failures)
    alive = [ln for ln in out.splitlines() if "every capture alive" in ln]
    check(
        "T3 toter Schwanz (main): alive-FAIL",
        bool(alive) and alive[0].startswith("- [FAIL]"),
        True,
        failures,
    )


def main() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as t:
        for fn, args in (
            (test_ack_path, ()),
            (test_liveness, ()),
            (test_missing_and_single_node, (Path(t),)),
        ):
            try:
                fn(failures, *args)
            except Exception as exc:  # noqa: BLE001 -- ein kaputter Test darf die anderen nicht verdecken
                failures.append(f"{fn.__name__}: {type(exc).__name__}: {exc}")
    if failures:
        print(f"FEHLGESCHLAGEN ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("test_soakstatus_fixes: alle Pruefungen bestanden (soakstatus.py T3).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
