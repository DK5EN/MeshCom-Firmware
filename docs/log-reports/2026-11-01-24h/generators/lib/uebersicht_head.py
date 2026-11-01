"""Data loading and small HTML helpers of the overview page.

Every number the page shows comes from <data>/{q2,q34,q5,q6,health}/results.json, <data>/master/master.tsv,
<data>/hb9/{q2,q34,q5,q6,health}/results.json, <data>/hb9/master/master.tsv
or <data>/extra/uebersicht_*.json (verifier numbers and the OE3XIA silence measurement).
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


class Data:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.q2 = self._j("q2")
        self.q34 = self._j("q34")
        self.q5 = self._j("q5")
        self.q6 = self._j("q6")
        self.health = self._j("health")
        with open(root / "master" / "master.tsv", encoding="utf-8") as fh:
            self.master = {r["call"]: r for r in csv.DictReader(fh, delimiter="\t")}
        self.ver = self._x("uebersicht_verified.json")
        self.sil = self._x("uebersicht_rx_silence.json")
        self.wjping = self._x("oe3xwj_ping.json")
        self.hb9x = self._x("uebersicht_hb9.json")
        self.hb9 = {q: self._j(f"hb9/{q}") for q in ("q2", "q34", "q5", "q6", "health")}
        with open(root / "hb9" / "master" / "master.tsv", encoding="utf-8") as fh:
            self.hb9_master = {r["call"]: r for r in csv.DictReader(fh, delimiter="\t")}

    def _j(self, name: str) -> dict[str, Any]:
        with open(self.root / name / "results.json", encoding="utf-8") as fh:
            return json.load(fh)

    def _x(self, name: str) -> dict[str, Any]:
        with open(self.root / "extra" / name, encoding="utf-8") as fh:
            return json.load(fh)


K: Any = None  # reportkit module, set by set_kit()


def set_kit(mod: Any) -> None:
    global K
    K = mod


NODES = [("oe3xhl", "OE3XHL-12"), ("oe3xia", "OE3XIA-12"), ("oe3xir", "OE3XIR-12"),
         ("oe3xoc", "OE3XOC-12"), ("oe3xwj", "OE3XWJ-12"), ("db0ais", "DB0AIS-12")]


HB9_NODES = [("hb9vqq4", "HB9VQQ-4"), ("hb9vqq5", "HB9VQQ-5")]


def de(x: float, d: int = 1) -> str:
    return f"{x:,.{d}f}".replace(",", "#").replace(".", ",").replace("#", ".")


def chip(kind: str, text: str | None = None) -> str:
    return K.chip(kind, text)


def ul(items: list[str]) -> str:
    return "<ul>" + "".join(f"<li>{i}</li>" for i in items) + "</ul>"


def ol(items: list[str]) -> str:
    return "<ol>" + "".join(f"<li>{i}</li>" for i in items) + "</ol>"


def tab(head: list[str], rows: list[list[Any]], num: tuple[int, ...] = (), raw: bool = True) -> str:
    return K.table(head, rows, num_cols=num, raw=raw)
