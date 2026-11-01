"""Network overview report (uebersicht.html) of the MeshCom 24-h log analysis.

Usage: python3 -I uebersicht.py --data <run>/data --kit <dir with reportkit.py> --out <file>
Reads only <data>/{q2,q34,q5,q6,health,master,extra}; helpers live in lib/uebersicht_*.py next to this file.
Stdlib only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
    sys.path.insert(0, str(a.kit.resolve()))
    import reportkit
    import uebersicht_body as body
    import uebersicht_head as head

    head.set_kit(reportkit)
    d = head.Data(a.data)
    _, figs = body.build(d, {})
    text, figs2 = body.build(d, figs)
    assert figs == figs2
    html = reportkit.page("Netzwerk-Übersicht · MeshCom Log-Auswertung 01.11.2026", text)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(html, encoding="utf-8")
    print(f"wrote {a.out} ({len(html)} bytes, {len(figs)} figures)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
