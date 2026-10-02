#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Testet ``tools/nbrsnap.py``, ``tools/nbrrelay.py`` und ``tools/nbrhopcheck.py`` gegen die
W2c-Erweiterung des Log-Vertrags (docs/nbr-logformat.md, Kantenpool-Umbau Welle 2,
docs/meshcom5-campaign.md):

- Masken bei ``NEED``/``CANCEL?``/``CANCEL``/``REFUSE`` sind jetzt 16 (klassischer ESP32) oder 32
  (S3/nRF52) Hex-Stellen statt 8 -- keines der drei Skripte parst diese Masken inhaltlich (nur
  ``tools/nbrlog.py`` zaehlt ihren Untertyp, siehe dessen eigenen ``--self-test``), sie muessen
  aber trotzdem klaglos durchlaufen, egal welche Breite.
- Die neue Zeile ``[NBR]|EVICT-E|<up>|<from>|<to>`` (eine einzelne verdraengte Kante, keine ganze
  Zeile) darf keines der drei Skripte mit einer Exception abbrechen lassen.

``tools/nbrlog.py`` hat dieselbe Fixture (``tools/testdata/nbr/nbr_sample_w2c.log``) bereits unter
seinem eigenen ``--self-test``; hier geht es nur um die drei Skripte, die keinen eigenen
Selbsttest haben.

Usage::

    uv run test/test_nbrlog/test_nbr_w2c_tools.py
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
FIXTURE = TOOLS_DIR / "testdata" / "nbr" / "nbr_sample_w2c.log"


def load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS_DIR / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
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

    # -- nbrsnap.py: EVICT-E muss die betroffene Kante loeschen (kein KeyError,
    #    kein Crash bei einer Kante, die es gar nicht gibt), und die 16-/32-Hex-
    #    NEED/CANCEL?/CANCEL/REFUSE-Zeilen (die dieses Skript ohnehin nicht
    #    anfasst) duerfen den Lauf nicht stoeren. --
    nbrsnap = load_module("nbrsnap")
    rc, out = run_capturing(nbrsnap.main, [str(FIXTURE)])
    if rc != 0:
        failures.append(f"nbrsnap.py: Exitcode {rc} statt 0 auf der W2c-Fixture")
    if "eigen: DK5EN-98" not in out:
        failures.append(f"nbrsnap.py: eigenes Rufzeichen nicht erkannt, Ausgabe:\n{out}")
    if "Verdraengungen von Direktzeilen: 0 von 0" not in out:
        failures.append(
            f"nbrsnap.py: EVICT-E darf nicht als Zeilen-EVICT gezaehlt werden (Fixture hat keine "
            f"[NBR]|EVICT-Zeile, nur EVICT-E):\n{out}"
        )

    # -- nbrrelay.py: dieselbe Toleranzpruefung. Die Fixture hat keine [LOG]-
    #    RX-Zeile, also 0 Frames -- das darf nicht mit einem IndexError enden. --
    nbrrelay = load_module("nbrrelay")
    rc, out = run_capturing(nbrrelay.main, ["--own", "DK5EN-98", str(FIXTURE)])
    if rc != 0:
        failures.append(f"nbrrelay.py: Exitcode {rc} statt 0 auf der W2c-Fixture")
    if "0 Frames" not in out:
        failures.append(f"nbrrelay.py: erwartet 0 RX-Frames (keine [LOG]-Zeilen in der Fixture):\n{out}")
    if "direkt gehoert ['DK5EN-93', 'DK5EN-94']" not in out:
        failures.append(f"nbrrelay.py: ME-Zeilen nicht als direkte Nachbarn erkannt:\n{out}")

    # -- nbrhopcheck.py: liest nur ME/EDGE/SNAP/ROW per Regex, NEED/CANCEL?/
    #    CANCEL/REFUSE/EVICT-E matchen die Regex nicht und muessen einfach
    #    uebersprungen werden (kein Crash, ein ganz normaler Befund-Exitcode). --
    nbrhopcheck = load_module("nbrhopcheck")
    rc, out = run_capturing(nbrhopcheck.main, [str(FIXTURE)])
    if rc not in (0, 1):
        failures.append(f"nbrhopcheck.py: unerwarteter Exitcode {rc} (0 oder 1 erwartet):\n{out}")
    if "Zeilen  1  Hop1" not in out:
        failures.append(f"nbrhopcheck.py: Schnappschuss nicht ausgewertet:\n{out}")

    if failures:
        print(f"FEHLGESCHLAGEN ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("test_nbr_w2c_tools: alle Pruefungen bestanden (nbrsnap.py, nbrrelay.py, nbrhopcheck.py).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
