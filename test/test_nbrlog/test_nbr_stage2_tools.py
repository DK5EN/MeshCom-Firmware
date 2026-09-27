#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Testet ``tools/nbrsnap.py`` und ``tools/nbrrelay.py`` gegen die Stage-2-Erweiterung
des Log-Vertrags (docs/nbr-logformat.md): <snr> bei ME/EDGE, [NBR]|SYM, <inferred> bei
NEED/CANCEL?/CANCEL. ``tools/nbrlog.py`` prueft sich selbst per ``--self-test`` gegen
dieselbe Fixture (``tools/testdata/nbr/nbr_sample_stage2.log``) -- hier geht es nur um
die beiden anderen Skripte, die keinen eigenen Selbsttest haben: sie muessen die neuen,
trailenden Felder klaglos tolerieren (nie als Parserfehler, nie mit einer Exception).

Usage::

    uv run test/test_nbrlog/test_nbr_stage2_tools.py
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
FIXTURE = TOOLS_DIR / "testdata" / "nbr" / "nbr_sample_stage2.log"


def load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS_DIR / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_capturing(func, argv: list[str]) -> tuple[int, str]:
    """Ruft ``func(argv)`` auf und liefert (returncode, stdout) -- faengt SystemExit ab."""
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            rc = func(argv)
    except SystemExit as exc:
        rc = 0 if exc.code is None else exc.code
    return (rc or 0), buf.getvalue()


def main() -> int:
    failures: list[str] = []

    if not FIXTURE.exists():
        print(f"Fixture fehlt: {FIXTURE}", file=sys.stderr)
        return 2

    # -- nbrsnap.py: neue trailende Felder bei ME/EDGE duerfen die
    #    Geraetesicht-Rekonstruktion nicht stoeren. Das Skript liest nur
    #    Anfangs-Indizes (frm/to/call), nie eine feste Feldzahl -- die neuen
    #    Felder muessen also schon rein positionell ignoriert werden. --
    nbrsnap = load_module("nbrsnap")
    rc, out = run_capturing(nbrsnap.main, [str(FIXTURE)])
    if rc != 0:
        failures.append(f"nbrsnap.py: Exitcode {rc} statt 0 auf der Stage-2-Fixture")
    if "eigen: DK5EN-98" not in out:
        failures.append(f"nbrsnap.py: eigenes Rufzeichen nicht erkannt, Ausgabe:\n{out}")
    if "DK5EN-93" not in out or "DK5EN-94" not in out:
        failures.append(f"nbrsnap.py: direkte Nachbarn fehlen in der Ausgabe:\n{out}")

    # -- nbrrelay.py: dieselbe Toleranzpruefung, plus die Randbedingung, dass
    #    ein Mitschnitt OHNE jede [LOG]-RX-Zeile (wie diese reine
    #    NBR-Fixture) nicht mit IndexError abstuerzen darf (rx_t leer). --
    nbrrelay = load_module("nbrrelay")
    rc, out = run_capturing(nbrrelay.main, ["--own", "DK5EN-98", str(FIXTURE)])
    if rc != 0:
        failures.append(f"nbrrelay.py: Exitcode {rc} statt 0 auf der Stage-2-Fixture")
    if "0 Frames" not in out:
        failures.append(f"nbrrelay.py: erwartet 0 RX-Frames (keine [LOG]-Zeilen in der Fixture):\n{out}")
    if "direkt gehoert ['DK5EN-93', 'DK5EN-94']" not in out:
        failures.append(
            f"nbrrelay.py: ME-Zeilen mit neuem <snr>-Feld nicht als direkte Nachbarn erkannt:\n{out}"
        )

    # -- own aus der SNAP-Zeile ableiten (kein --own) muss ebenfalls klappen --
    rc, out = run_capturing(nbrrelay.main, [str(FIXTURE)])
    if rc != 0:
        failures.append(f"nbrrelay.py ohne --own: Exitcode {rc} statt 0")
    if "eigen: DK5EN-98" not in out:
        failures.append(f"nbrrelay.py ohne --own: eigenes Rufzeichen nicht aus SNAP abgeleitet:\n{out}")

    if failures:
        print(f"FEHLGESCHLAGEN ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("test_nbr_stage2_tools: alle Pruefungen bestanden (nbrsnap.py, nbrrelay.py).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
