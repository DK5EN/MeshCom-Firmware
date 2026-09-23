#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Auswerter fuer den [NBR]-Mitschnitt der Nachbarschaftsmatrix (24-h-Dauertest).

Format-Vertrag: ``docs/nbr-logformat.md`` (Firmware- und Parser-Seite gemeinsam
gepflegt). Wer eine Zeile dort aendert, aendert auch dieses Skript.

Die Firmware schreibt ``[NBR]|...``-Zeilen auf die Netz-Debug-Konsole
(TCP 2323). ``tools/meshlogger.py`` schneidet sie mit einem vorangestellten
Host-Zeitstempel mit -- ``YYYY-MM-DD HH:MM:SS.mmm<zwei Leerzeichen><Text>`` --
in taeglich rotierende Dateien. Dieses Skript liest diese Dateien (lokal oder
per ``--fetch`` von einem Pi geholt) und beantwortet die fachliche Frage, ob
ein direkt gehoerter Nachbar selbst meshen muss -- er hoert Knoten, die weder
ich selbst direkt noch ein anderer meiner Nachbarn abdeckt (Firmware-
``<meshneed>`` == ``MESH``) -- oder ob sein Meshen redundant ist, weil alles,
was er hoert, anderweitig gedeckt ist (``RED``). Das ist eine andere Frage als
das Firmware-``<verdict>`` (docs/nbr-logformat.md, Fund 1 des Advisor-Passes
2026-09-21) und wird auch dagegen verglichen, nicht gegen ``<verdict>``.

Der Parser ist bewusst tolerant: eine einzelne kaputte Zeile (fremder
Zeitstempel, abgeschnittenes Dateiende, verschluckte Zeichen aus einer
Reconnect-Luecke, unbekannter [NBR]-Untertyp, irgendeine andere Konsolenzeile)
bricht den Lauf nie ab. Jede Zeile, die nicht zu einem erfolgreich geparsten
[NBR]-Datensatz wird, zaehlt unter "verworfen", mit Grund.

Usage::

    uv run tools/nbrlog.py ~/meshlog/dk5en-98/*.log --out bericht.md --json bericht.json
    uv run tools/nbrlog.py --fetch martin@rpizero.local:~/meshlog/dk5en-98/ --dry-run
    uv run tools/nbrlog.py --fetch martin@rpizero.local:~/meshlog/dk5en-98/ --out bericht.md
    uv run tools/nbrlog.py --self-test
"""

from __future__ import annotations

import argparse
import gzip
import json
import math
import re
import shlex
import shutil
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Any

# --------------------------------------------------------------------------
# Konstanten
# --------------------------------------------------------------------------

#: Host-Zeitstempel-Praefix, wie ``tools/meshlogger.py``s ``Sink.write()`` ihn
#: erzeugt: ``stamp() + "  " + text``. ``stamp()`` ist
#: ``"%Y-%m-%d %H:%M:%S.%f"[:-3]`` -- Millisekunden, kein Mikrosekunden-Rest.
RE_PREFIX = re.compile(
    r"^(?P<host>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s\s(?P<rest>.*)$"
)

NBR_MARKER = "[NBR]|"

#: Zeitstempelspruenge ab dieser Dauer gelten als Mitschnitt-Luecke.
GAP_THRESHOLD_S = 120.0

EARTH_RADIUS_KM = 6371.0088

#: Bekannte Firmware-Urteile aus docs/nbr-logformat.md (nur zur Anzeige, kein
#: hartes Gate -- ein unbekannter Wert wird einfach durchgereicht).
KNOWN_VERDICTS = frozenset({"EXCL", "RED", "LEAF", "UNK"})

#: Bekannte Werte von <meshneed> (docs/nbr-logformat.md) -- ebenfalls nur zur
#: Anzeige. ``None`` (Feld fehlt, altes Firmware-Format) ist gesondert erlaubt
#: und steht NICHT in dieser Menge.
KNOWN_MESHNEED = frozenset({"MESH", "RED", "NA"})

#: Bekannte Werte von <status> bei [NBR]|RPT (docs/nbr-logformat.md, HN-Bericht)
#: -- nur zur Anzeige, kein hartes Gate.
KNOWN_RPT_STATUS = frozenset({"ok", "self", "norow"})


def fmt_dt(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%d %H:%M:%S.") + f"{dt.microsecond // 1000:03d}"


def hour_bucket(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%d %H")


class _Discard(Exception):
    """Signalisiert dem Dispatcher: diese Zeile strukturell verworfen, mit Grund."""

    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


# --------------------------------------------------------------------------
# Datensaetze
# --------------------------------------------------------------------------


@dataclass
class MeRec:
    host: datetime
    up: int
    frm: str
    type_: str
    rssi: int
    cnt: int
    session: int
    #: SNR in dB fuer "ich habe <frm> gehoert" (docs/nbr-logformat.md). ``None``
    #: heisst: entweder ein Mitschnitt aelterer Firmware ohne dieses Feld, oder
    #: die Firmware selbst kannte keine SNR (``NA``) -- beide Faelle sind fuer
    #: die Auswertung gleich ("kein Wert"), sie werden nicht unterschieden.
    snr: int | None = None


@dataclass
class EdgeRec:
    host: datetime
    up: int
    frm: str
    to: str
    type_: str
    rssi: int
    cnt: int
    session: int
    #: SNR in dB fuer "<to> hat <frm> gehoert" (aus HEY-Berichten, kann aelter
    #: sein als diese Zeile). ``None`` wie bei ``MeRec.snr``. ``rssi`` ist in
    #: neuen Mitschnitten immer 0 -- die Zelle speichert keine RSSI mehr.
    snr: int | None = None


@dataclass
class CutRec:
    host: datetime
    up: int
    ntok: int
    kept: int
    path: list[str]
    session: int


@dataclass
class DropRec:
    host: datetime
    up: int
    reason: str
    path: list[str]
    session: int


@dataclass
class EvictRec:
    host: datetime
    up: int
    idx: int
    old: str
    new: str
    session: int


@dataclass
class PosRec:
    host: datetime
    up: int
    call: str
    lat: float
    lon: float
    mesh: int
    hw: int
    session: int


@dataclass
class RowRec:
    idx: int
    call: str
    flags: int
    age: int
    hearers: int
    verdict: str
    #: Betreiberfrage "muss dieser Knoten selbst meshen" (docs/nbr-logformat.md).
    #: ``None`` heisst: die Zeile kam aus einem Mitschnitt mit aelterer
    #: Firmware ohne dieses Feld -- kein Fehler, siehe ``_h_row``.
    meshneed: str | None = None


@dataclass
class SymRec:
    """Eine geloggte ``--nbrsym``-Annahme ODER ein ``VETO`` (docs/nbr-logformat.md,
    ``[NBR]|SYM``).

    Fuer die Rollen ``HASF``/``ALT``/``COVER`` nur geschrieben, wenn die
    Annahme das Ergebnis einer Stufe-2-Relay-Entscheidung tatsaechlich
    veraendert hat -- ``<snr>`` ist die SNR, mit der ``<m>`` ``<x>`` gehoert
    hat (nicht umgekehrt: die Annahme LEITET aus dieser Kante "<x> hoert <m>"
    her, siehe ``<role>``). Die Rolle ``VETO`` ist das GEGENTEIL einer
    angewendeten Annahme: sie zaehlt NICHT unter Abschnitt 10
    (Symmetrie-Annahmen), sondern unter Abschnitt 11 (HN-Nachbarschafts-
    meldungen) -- eine Annahme, die angewendet WORDEN WAERE, wurde durch
    ``<x>``s vollstaendigen frischen HN-Bericht blockiert.
    """

    host: datetime
    up: int
    msg_id: str
    role: str
    x: str
    m: str
    snr: int
    session: int


@dataclass
class RptRec:
    """Eine eingetragene/verworfene HN-Berichtszeile (docs/nbr-logformat.md,
    ``[NBR]|RPT``). ``<status>`` ist ``ok`` (Kante eingetragen), ``self``
    (``<m>`` bin ich selbst: "<x> hat mich gehoert") oder ``norow`` (``<m>``
    hat keine Matrixzeile, ignoriert)."""

    host: datetime
    up: int
    x: str
    m: str
    snr: int
    status: str
    session: int


@dataclass
class RptSumRec:
    """Zusammenfassung eines empfangenen HN-Berichts (docs/nbr-logformat.md,
    ``[NBR]|RPTSUM``). ``full`` ist die Vollstaendigkeit des Berichts (kein
    ``+`` im Frame -- ``True`` heisst, ``<x>`` hoert AUSSER den ``<k>``
    gelisteten Stationen keine weitere mit SNR >= ``LORA_SNR_STABLE_MIN_DB``).
    ``heard`` ist die MHeard-Zahl des Senders (wie ``R<n>`` im HEY), kein
    Kappungssignal -- das ist allein ``full``.
    ``applied`` ist die Anzahl der daraus tatsaechlich eingetragenen Kanten
    (Status ``ok`` unter den zugehoerigen RPT-Zeilen; ``self``/``norow``
    zaehlen nicht mit)."""

    host: datetime
    up: int
    x: str
    heard: int
    k: int
    full: bool
    applied: int
    session: int


@dataclass
class RptTxRec:
    """Ein selbst gesendeter HN-Bericht (docs/nbr-logformat.md,
    ``[NBR]|RPTTX``). ``length`` ist die vom Sender selbst berichtete
    On-Air-Laenge des Payloads -- massgeblich, nicht ``len(payload)`` (die
    Netz-Konsole koennte das Feld theoretisch verstuemmeln)."""

    host: datetime
    up: int
    length: int
    payload: str
    session: int


@dataclass
class SnapBlock:
    host: datetime
    up: int
    own: str
    rows: int
    maxrows: int
    cells: int
    session: int
    rows_entries: list[RowRec] = field(default_factory=list)
    incomplete: bool = False


@dataclass
class NbrState:
    """Akkumulierter Zustand waehrend des Parsens, ueber alle Dateien hinweg."""

    total_lines: int = 0
    nbr_lines: int = 0
    filtered_out: int = 0
    discard_reasons: Counter = field(default_factory=Counter)
    anomalies: Counter = field(default_factory=Counter)

    first_host: datetime | None = None
    last_host: datetime | None = None
    last_ts_any: datetime | None = None
    gaps: list[dict[str, Any]] = field(default_factory=list)

    last_up: int | None = None
    session: int = 0
    reboots: int = 0
    reboot_events: list[dict[str, Any]] = field(default_factory=list)

    me: list[MeRec] = field(default_factory=list)
    edges: list[EdgeRec] = field(default_factory=list)
    syms: list[SymRec] = field(default_factory=list)
    #: HN-Nachbarschaftsmeldung (docs/nbr-logformat.md, ``--nbrreport``).
    rpts: list[RptRec] = field(default_factory=list)
    rptsums: list[RptSumRec] = field(default_factory=list)
    rpttx: list[RptTxRec] = field(default_factory=list)
    cuts: list[CutRec] = field(default_factory=list)
    drops: list[DropRec] = field(default_factory=list)
    evicts: list[EvictRec] = field(default_factory=list)
    pos: list[PosRec] = field(default_factory=list)
    snaps: list[SnapBlock] = field(default_factory=list)
    open_snap: SnapBlock | None = None

    files: list[str] = field(default_factory=list)


# --------------------------------------------------------------------------
# Zeilen-Handler -- ein Handler pro [NBR]-Untertyp
# --------------------------------------------------------------------------


def _parse_snr(raw: str | None) -> int | None:
    """``NA`` oder ein fehlendes (altes) Feld werden beide zu ``None``."""
    if raw is None or raw == "NA":
        return None
    return int(raw)


def _h_me(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if len(f) == 5:
        # Aktuelles Format: <from>|<type>|<rssi>|<cnt>|<snr>.
        (frm, type_, rssi, cnt, snr_raw) = f
    elif len(f) == 4:
        # Aelteres Firmware-Format ohne <snr> -- rueckwaertskompatibel.
        (frm, type_, rssi, cnt) = f
        snr_raw = None
    else:
        raise _Discard("malformed:ME")
    state.me.append(
        MeRec(host, up, frm, type_, int(rssi), int(cnt), state.session, _parse_snr(snr_raw))
    )


def _h_edge(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if len(f) == 6:
        # Aktuelles Format: <from>|<to>|<type>|<rssi>|<cnt>|<snr>.
        (frm, to, type_, rssi, cnt, snr_raw) = f
    elif len(f) == 5:
        # Aelteres Firmware-Format ohne <snr> -- rueckwaertskompatibel.
        (frm, to, type_, rssi, cnt) = f
        snr_raw = None
    else:
        raise _Discard("malformed:EDGE")
    state.edges.append(
        EdgeRec(
            host, up, frm, to, type_, int(rssi), int(cnt), state.session,
            _parse_snr(snr_raw),
        )
    )


def _h_sym(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if len(f) != 5:
        raise _Discard("malformed:SYM")
    (msg_id, role, x, m, snr) = f
    state.syms.append(SymRec(host, up, msg_id, role, x, m, int(snr), state.session))


def _h_rpt(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if len(f) != 4:
        raise _Discard("malformed:RPT")
    (x, m, snr, status) = f
    state.rpts.append(RptRec(host, up, x, m, int(snr), status, state.session))


def _h_rptsum(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if len(f) != 5:
        raise _Discard("malformed:RPTSUM")
    (x, heard, k, full, applied) = f
    state.rptsums.append(
        RptSumRec(host, up, x, int(heard), int(k), bool(int(full)), int(applied), state.session)
    )


def _h_rpttx(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if len(f) != 2:
        raise _Discard("malformed:RPTTX")
    (length, payload) = f
    state.rpttx.append(RptTxRec(host, up, int(length), payload, state.session))


def _h_cut(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    (ntok, kept, path) = f
    path_list = [c for c in path.split(",") if c]
    state.cuts.append(
        CutRec(host, up, int(ntok), int(kept), path_list, state.session)
    )


def _h_drop(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    (reason, path) = f
    path_list = [c for c in path.split(",") if c]
    state.drops.append(DropRec(host, up, reason, path_list, state.session))


def _h_evict(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    (idx, old, new) = f
    state.evicts.append(
        EvictRec(host, up, int(idx), old, new, state.session)
    )


def _h_pos(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    (call, lat, lon, mesh, hw) = f
    state.pos.append(
        PosRec(host, up, call, float(lat), float(lon), int(mesh), int(hw), state.session)
    )


def _h_snap(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    (own, rows, maxrows, cells) = f
    if state.open_snap is not None:
        # Ein neuer SNAP kommt, ohne dass der vorige per ENDSNAP geschlossen
        # wurde (z. B. eine Zeile in der Luecke verloren). Das ist eine
        # Anomalie, kein Zeilenfehler -- der neue SNAP selbst ist gueltig.
        state.anomalies["snap_not_closed"] += 1
        state.open_snap.incomplete = True
        state.snaps.append(state.open_snap)
    state.open_snap = SnapBlock(
        host, up, own, int(rows), int(maxrows), int(cells), state.session
    )


def _h_row(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if state.open_snap is None:
        raise _Discard("row_without_snap")
    if len(f) == 7:
        # Aktuelles Format: <idx>|<call>|<flags>|<age>|<hearers>|<verdict>|<meshneed>.
        (idx, call, flags, age, hearers, verdict, meshneed) = f
    elif len(f) == 6:
        # Aelteres Firmware-Format ohne <meshneed> -- rueckwaertskompatibel,
        # KEIN Fehler (docs/nbr-logformat.md, Fund 1 des Advisor-Passes 2026-09-21).
        (idx, call, flags, age, hearers, verdict) = f
        meshneed = None
    else:
        raise _Discard("malformed:ROW")
    state.open_snap.rows_entries.append(
        RowRec(int(idx), call, int(flags), int(age), int(hearers), verdict, meshneed)
    )


def _h_endsnap(state: NbrState, host: datetime, up: int, f: list[str]) -> None:
    if f:
        raise _Discard("malformed:ENDSNAP")
    if state.open_snap is None:
        raise _Discard("endsnap_without_snap")
    state.snaps.append(state.open_snap)
    state.open_snap = None


HANDLERS = {
    "ME": _h_me,
    "EDGE": _h_edge,
    "SYM": _h_sym,
    "RPT": _h_rpt,
    "RPTSUM": _h_rptsum,
    "RPTTX": _h_rpttx,
    "CUT": _h_cut,
    "DROP": _h_drop,
    "EVICT": _h_evict,
    "POS": _h_pos,
    "SNAP": _h_snap,
    "ROW": _h_row,
    "ENDSNAP": _h_endsnap,
}


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------


def open_maybe_gz(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("r", encoding="utf-8", errors="replace")


def parse_nbr_line(state: NbrState, host: datetime, rest: str) -> None:
    """Parst den Teil einer Zeile nach dem Host-Zeitstempel.

    Wirft nichts nach aussen -- jeder Fehlerpfad zaehlt unter
    ``state.discard_reasons`` und kehrt zurueck.
    """
    idx = rest.find(NBR_MARKER)
    if idx == -1:
        state.discard_reasons["foreign_line"] += 1
        return
    if idx != 0:
        # Zeichen vor dem Marker verschluckt/verstuemmelt (Reconnect-Luecke).
        # Ein Teilrekonstrukt waere Ratewerk -- die ganze Zeile faellt weg.
        state.discard_reasons["garbled_prefix"] += 1
        return

    body = rest[len(NBR_MARKER):]
    tail = body.find(NBR_MARKER)
    if tail != -1:
        # Zwei Konsolenzeilen ohne Newline dazwischen verschweisst (ebenfalls
        # eine Reconnect-Luecke). Nicht raten, welcher Teil "richtig" ist.
        state.discard_reasons["glued_line"] += 1
        return

    parts = body.split("|")
    if len(parts) < 2:
        state.discard_reasons["malformed:short"] += 1
        return
    subtype = parts[0]
    handler = HANDLERS.get(subtype)
    if handler is None:
        state.discard_reasons[f"unknown_subtype:{subtype}"] += 1
        return
    try:
        up = int(parts[1])
    except ValueError:
        state.discard_reasons[f"malformed:{subtype}"] += 1
        return

    fields = parts[2:]

    if state.last_up is not None and up < state.last_up:
        state.reboots += 1
        state.session += 1
        state.reboot_events.append(
            {"host": fmt_dt(host), "up_vorher": state.last_up, "up_nachher": up}
        )
    state.last_up = up

    try:
        handler(state, host, up, fields)
    except _Discard as exc:
        state.discard_reasons[exc.reason] += 1
        return
    except (ValueError, IndexError):
        state.discard_reasons[f"malformed:{subtype}"] += 1
        return
    state.nbr_lines += 1


def process_line(state: NbrState, line: str, since: date | None) -> None:
    line = line.rstrip("\n").rstrip("\r")
    if not line:
        return

    m = RE_PREFIX.match(line)
    if not m:
        state.total_lines += 1
        state.discard_reasons["no_timestamp"] += 1
        return

    host = datetime.strptime(m.group("host"), "%Y-%m-%d %H:%M:%S.%f")
    if since is not None and host.date() < since:
        state.filtered_out += 1
        return

    state.total_lines += 1
    if state.first_host is None:
        state.first_host = host
    state.last_host = host

    if state.last_ts_any is not None:
        delta = (host - state.last_ts_any).total_seconds()
        if delta > GAP_THRESHOLD_S:
            state.gaps.append(
                {
                    "von": fmt_dt(state.last_ts_any),
                    "bis": fmt_dt(host),
                    "dauer_s": round(delta, 1),
                }
            )
    state.last_ts_any = host

    parse_nbr_line(state, host, m.group("rest"))


def parse_files(paths: list[Path], since: date | None = None) -> NbrState:
    state = NbrState()
    for path in sorted(paths, key=lambda p: p.name):
        state.files.append(str(path))
        with open_maybe_gz(path) as fh:
            for line in fh:
                process_line(state, line, since)
    return state


# --------------------------------------------------------------------------
# Haversine
# --------------------------------------------------------------------------


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = (
        math.sin(dphi / 2) ** 2
        + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2) ** 2
    )
    return 2 * EARTH_RADIUS_KM * math.asin(math.sqrt(a))


# --------------------------------------------------------------------------
# Analyse -- ein Abschnitt pro Nummer aus dem Auftrag
# --------------------------------------------------------------------------


def a1_rahmen(state: NbrState) -> dict[str, Any]:
    return {
        "dateien": state.files,
        "von": fmt_dt(state.first_host) if state.first_host else None,
        "bis": fmt_dt(state.last_host) if state.last_host else None,
        "zeilen_gesamt": state.total_lines,
        "nbr_zeilen": state.nbr_lines,
        "verworfen_gesamt": state.total_lines - state.nbr_lines,
        "verworfen_gruende": dict(sorted(state.discard_reasons.items())),
        "gefiltert_durch_since": state.filtered_out,
        "anomalien": dict(sorted(state.anomalies.items())),
        "reboots": state.reboots,
        "reboot_ereignisse": state.reboot_events,
        "luecken": state.gaps,
        "anzahl_luecken": len(state.gaps),
    }


def own_call_of(state: NbrState) -> str | None:
    c: Counter = Counter(s.own for s in state.snaps)
    if not c:
        return None
    return c.most_common(1)[0][0]


def a2_nachbarschaft(state: NbrState) -> dict[str, Any]:
    by_call: dict[str, list[MeRec]] = defaultdict(list)
    for r in state.me:
        by_call[r.frm].append(r)

    rows: list[dict[str, Any]] = []
    for call, recs in by_call.items():
        rssi_vals = [r.rssi for r in recs if r.rssi != 0]
        # <snr> ist ein neues Feld (docs/nbr-logformat.md) -- ``None`` deckt
        # sowohl "NA" als auch "Mitschnitt ohne dieses Feld" ab, beides zaehlt
        # hier gleich als "kein Wert".
        snr_vals = [r.snr for r in recs if r.snr is not None]
        rows.append(
            {
                "rufzeichen": call,
                "anzahl": len(recs),
                "typverteilung": dict(Counter(r.type_ for r in recs)),
                "rssi_median": round(statistics.median(rssi_vals), 1) if rssi_vals else None,
                "rssi_min": min(rssi_vals) if rssi_vals else None,
                "rssi_max": max(rssi_vals) if rssi_vals else None,
                "rssi_ohne_bericht": len(recs) - len(rssi_vals),
                "snr_median": round(statistics.median(snr_vals), 1) if snr_vals else None,
                "snr_min": min(snr_vals) if snr_vals else None,
                "snr_max": max(snr_vals) if snr_vals else None,
                "snr_ohne_bericht": len(recs) - len(snr_vals),
                "erste_sichtung": fmt_dt(min(r.host for r in recs)),
                "letzte_sichtung": fmt_dt(max(r.host for r in recs)),
            }
        )
    rows.sort(key=lambda d: -d["anzahl"])
    return {"anzahl_direkte_nachbarn": len(rows), "je_nachbar": rows}


def direct_neighbours(state: NbrState) -> list[str]:
    return sorted({r.frm for r in state.me})


def heard_sets(
    state: NbrState, neighbours: list[str], exclude: str | None = None
) -> dict[str, set[str]]:
    """Fuer jeden direkten Nachbarn die Menge der Knoten, die er gehoert hat.

    Quelle: EDGE-Zeilen mit ``<to>`` == dieser Nachbar (docs/nbr-logformat.md:
    "<to> hat <from> gehoert"). Ein Selbstbezug (``<from> == <to>``) zaehlt nie
    mit. Mit ``exclude`` faellt zusaetzlich ein bestimmter Rufname aus jeder
    Menge heraus -- fuer Abschnitt 4 der eigene Call (Fund 2b des
    Advisor-Passes 2026-09-21: dass ein Nachbar MICH gehoert hat, macht ihn
    nicht unverzichtbar).
    """
    out: dict[str, set[str]] = {n: set() for n in neighbours}
    for e in state.edges:
        if e.to not in out:
            continue
        if e.frm == e.to:
            continue
        if exclude is not None and e.frm == exclude:
            continue
        out[e.to].add(e.frm)
    return out


def a3_kreuzmatrix(state: NbrState, neighbours: list[str], heard: dict[str, set[str]]) -> dict[str, Any]:
    je_nachbar = [
        {"rufzeichen": n, "anzahl_gehoert": len(heard[n]), "gehoert": sorted(heard[n])}
        for n in neighbours
    ]
    inverse: dict[str, set[str]] = defaultdict(set)
    for n, s in heard.items():
        for node in s:
            inverse[node].add(n)
    umgekehrt = [
        {"rufzeichen": node, "gehoert_von": sorted(hearers)}
        for node, hearers in sorted(inverse.items())
    ]
    return {"je_nachbar": je_nachbar, "umgekehrt": umgekehrt}


def latest_firmware_row(
    state: NbrState, call: str
) -> tuple[str | None, str | None, int | None]:
    """(<verdict>, <meshneed>, snapshot-up) aus dem letzten Snapshot mit ``call``.

    Beide Felder kommen aus derselben ROW-Zeile -- nie aus verschiedenen
    Snapshots gemischt.
    """
    for snap in reversed(state.snaps):
        for row in snap.rows_entries:
            if row.call == call:
                return row.verdict, row.meshneed, snap.up
    return None, None, None


def a4_urteil(
    state: NbrState,
    neighbours: list[str],
    heard: dict[str, set[str]],
    own_heard: set[str],
) -> dict[str, Any]:
    """Abschnitt 4 -- Betreiberfrage "muss dieser direkte Nachbar selbst meshen?".

    Deckungsregel (docs/nbr-logformat.md, Fund 2 des Advisor-Passes
    2026-09-21, ``heard`` liefert H(n) bereits gefiltert -- ohne n selbst,
    ohne den eigenen Call): ein Knoten aus H(n) gilt als abgedeckt, wenn ich
    ihn selbst direkt hoere (``own_heard``, aus den ME-Zeilen) ODER ein
    anderer direkter Nachbar ihn hoert. Bleibt mindestens ein Knoten
    unabgedeckt, ist n "exklusiv" (Firmware-``MESH``), sonst "redundant"
    (Firmware-``RED``). Leeres H(n) ist immer "redundant".

    Verglichen wird gegen ``<meshneed>``, NICHT gegen ``<verdict>`` -- Fund 1:
    die beiden beantworten entgegengesetzte Fragen an derselben Kante.
    ``<verdict>`` wird nur mitgefuehrt, damit der Bericht es anzeigen kann.
    """
    rows: list[dict[str, Any]] = []
    for n in neighbours:
        own_set = heard[n]
        others_union: set[str] = set()
        for m in neighbours:
            if m != n:
                others_union |= heard[m]
        covered = own_heard | others_union
        uncovered = own_set - covered
        mine = "exklusiv" if uncovered else "redundant"
        mine_meshneed = "MESH" if mine == "exklusiv" else "RED"
        fw_verdict, fw_meshneed, fw_up = latest_firmware_row(state, n)
        if fw_meshneed is None or fw_meshneed == "NA":
            # None: kein Snapshot / altes Format. "NA" waere fuer einen
            # direkten Nachbarn ohnehin unerwartet (die Frage ist fuer ihn
            # IMMER gestellt) -- in beiden Faellen nichts zu vergleichen.
            match: bool | None = None
        else:
            match = mine_meshneed == fw_meshneed
        rows.append(
            {
                "rufzeichen": n,
                "eigenes_urteil": mine,
                "eigenes_meshneed": mine_meshneed,
                "h_n": sorted(own_set),
                "exklusive_knoten": sorted(uncovered),
                "firmware_meshneed": fw_meshneed,
                "firmware_verdict": fw_verdict,
                "firmware_snapshot_up": fw_up,
                "uebereinstimmung": match,
            }
        )
    # "widerspricht sich" und "gar nicht vergleichbar" sind zwei verschiedene
    # Befunde und duerfen nicht in einen Topf -- genau diese Verwechslung war
    # der Advisor-Fund vom 2026-09-21 eine Ebene hoeher. uebereinstimmung is
    # False heisst, die Firmware ist anderer Meinung als diese Rechnung (ein
    # echter Fund). None heisst, es gab nichts zu vergleichen: kein Snapshot,
    # altes Firmware-Format ohne <meshneed>, oder der Nachbar stand zum
    # Snapshot-Zeitpunkt gar nicht in der Matrix (Tabellendruck) -- selbst
    # interessant, aber kein Widerspruch.
    abweichungen = [r for r in rows if r["uebereinstimmung"] is False]
    nicht_vergleichbar = [r for r in rows if r["uebereinstimmung"] is None]
    return {"je_nachbar": rows, "abweichungen": abweichungen,
            "nicht_vergleichbar": nicht_vergleichbar}


def a4b_deckung(
    neighbours: list[str],
    heard: dict[str, set[str]],
    own_heard: set[str],
    urteil_rows: list[dict[str, Any]],
) -> dict[str, Any]:
    """Abschnitt 4b -- Fund 3 des Advisor-Passes 2026-09-21.

    Die paarweise Rechnung aus ``a4_urteil`` beantwortet nur "ist DIESER eine
    Nachbar verzichtbar, wenn alle anderen bleiben?". Hoeren zwei Nachbarn
    exakt dieselbe (sonst von niemandem gehoerte) Menge, gilt in dieser
    Rechnung JEDER fuer sich als redundant -- schaltet man aber beide ab,
    fehlt die Menge. Dieser Abschnitt macht das ehrlich:

    - eine minimale Deckungsmenge (Greedy -- NICHT beweisbar minimal, siehe
      Kommentar unten), die alles abdeckt, was ueberhaupt ein Nachbar hoert
      und ich nicht ohnehin selbst direkt hoere;
    - die wechselseitig redundanten Gruppen: Nachbarn, die einzeln als
      redundant gelten, aber nicht alle gleichzeitig entbehrlich sind.
    """
    contrib = {n: (heard[n] - own_heard) for n in neighbours}
    universe: set[str] = set()
    for s in contrib.values():
        universe |= s

    # Greedy-Mengenueberdeckung: waehlt wiederholt den Nachbarn mit dem
    # groessten noch offenen Beitrag. Das ist eine obere Schranke, KEIN
    # beweisbares Minimum (Set Cover ist NP-schwer) -- bei den kleinen
    # Nachbarmengen eines Knotens (typischerweise < 30) ist der Unterschied
    # zum Optimum in der Praxis vernachlaessigbar, aber unbewiesen.
    remaining = set(universe)
    chosen: list[str] = []
    while remaining:
        n_best: str | None = None
        gain_best: set[str] = set()
        for n in neighbours:
            gain = contrib[n] & remaining
            if len(gain) > len(gain_best):
                n_best, gain_best = n, gain
        if n_best is None:
            break
        chosen.append(n_best)
        remaining -= gain_best
    chosen.sort()

    providers: dict[str, set[str]] = defaultdict(set)
    for n in neighbours:
        for x in contrib[n]:
            providers[x].add(n)

    redundant = {r["rufzeichen"] for r in urteil_rows if r["eigenes_urteil"] == "redundant"}

    # Graph nur ueber einzeln-redundante Nachbarn: eine Kante (a, b) entsteht,
    # wenn a und b (und sonst niemand ausser evtl. weiteren redundanten
    # Nachbarn) gemeinsam einen Knoten abdecken. Zusammenhangskomponenten
    # dieses Graphen sind die wechselseitig redundanten Gruppen. Das kann bei
    # Ketten ueber mehrere geteilte Knoten groesser gruppieren, als fuer einen
    # einzelnen Knoten noetig waere -- ``gemeinsame_knoten`` zeigt, WELCHE
    # Knoten die Abhaengigkeit tatsaechlich erzeugen.
    adjacency: dict[str, set[str]] = defaultdict(set)
    causes: dict[frozenset[str], set[str]] = defaultdict(set)
    for x, provs in providers.items():
        shared = provs & redundant
        if len(shared) >= 2:
            for a in shared:
                for b in shared:
                    if a != b:
                        adjacency[a].add(b)
            causes[frozenset(shared)].add(x)

    visited: set[str] = set()
    groups: list[dict[str, Any]] = []
    for n in sorted(redundant):
        if n in visited or n not in adjacency:
            continue
        stack = [n]
        comp: set[str] = set()
        while stack:
            cur = stack.pop()
            if cur in comp:
                continue
            comp.add(cur)
            stack.extend(adjacency[cur] - comp)
        visited |= comp
        if len(comp) >= 2:
            shared_nodes: set[str] = set()
            for key, nodes in causes.items():
                if key <= comp:
                    shared_nodes |= nodes
            groups.append(
                {"nachbarn": sorted(comp), "gemeinsame_knoten": sorted(shared_nodes)}
            )

    return {
        "universum_groesse": len(universe),
        "deckungsmenge": chosen,
        "deckungsmenge_groesse": len(chosen),
        "wechselseitig_redundante_gruppen": groups,
    }


def a5_stabilitaet(state: NbrState, neighbours: list[str]) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for n in neighbours:
        verlauf: list[str] = []
        for snap in state.snaps:
            for row in snap.rows_entries:
                if row.call == n:
                    verlauf.append(row.verdict)
                    break
        if not verlauf:
            rows.append(
                {
                    "rufzeichen": n,
                    "verlauf": [],
                    "anzahl_snapshots": 0,
                    "stabil_ab_snapshot": None,
                    "letztes_urteil": None,
                }
            )
            continue
        final = verlauf[-1]
        idx = len(verlauf) - 1
        while idx > 0 and verlauf[idx - 1] == final:
            idx -= 1
        rows.append(
            {
                "rufzeichen": n,
                "verlauf": verlauf,
                "anzahl_snapshots": len(verlauf),
                "stabil_ab_snapshot": idx + 1,
                "letztes_urteil": final,
            }
        )
    return {"je_nachbar": rows}


def a6_tabellendruck(state: NbrState) -> dict[str, Any]:
    reihen = [
        {
            "host": fmt_dt(s.host),
            "up": s.up,
            "rows": s.rows,
            "maxrows": s.maxrows,
            "voll": s.rows >= s.maxrows,
        }
        for s in state.snaps
    ]
    rows_vals = [s.rows for s in state.snaps]
    overflow = [r for r in reihen if r["voll"]]
    evict_pro_stunde: Counter = Counter(hour_bucket(e.host) for e in state.evicts)
    verdraengt: Counter = Counter(e.old for e in state.evicts)
    return {
        "reihen": reihen,
        "rows_max": max(rows_vals) if rows_vals else None,
        "rows_median": round(statistics.median(rows_vals), 1) if rows_vals else None,
        "ueberlauf_snapshots": overflow,
        "ueberlauf_erkannt": bool(overflow),
        "evict_pro_stunde": dict(sorted(evict_pro_stunde.items())),
        "verdraengte_rufzeichen": dict(verdraengt.most_common()),
        "evict_gesamt": len(state.evicts),
    }


def a7_zwei_hop_schnitt(state: NbrState) -> dict[str, Any]:
    dropped_tokens = [c.ntok - c.kept for c in state.cuts]
    return {
        "frames_gekuerzt": len(state.cuts),
        "token_verworfen_gesamt": sum(dropped_tokens),
        "verteilung_ntok": dict(sorted(Counter(c.ntok for c in state.cuts).items())),
        "verteilung_verworfene_token": dict(sorted(Counter(dropped_tokens).items())),
    }


def a8_verworfene_frames(state: NbrState) -> dict[str, Any]:
    pro_grund: Counter = Counter(d.reason for d in state.drops)
    pro_stunde_grund: dict[str, Counter] = defaultdict(Counter)
    for d in state.drops:
        pro_stunde_grund[hour_bucket(d.host)][d.reason] += 1
    full_count = pro_grund.get("FULL", 0)
    return {
        "pro_grund": dict(sorted(pro_grund.items())),
        "pro_stunde_und_grund": {h: dict(c) for h, c in sorted(pro_stunde_grund.items())},
        "full_anzahl": full_count,
        "full_alarm": full_count > 0,
    }


def a9_positionen(state: NbrState, own_call: str | None) -> dict[str, Any]:
    last: dict[str, PosRec] = {}
    for p in state.pos:
        cur = last.get(p.call)
        if cur is None or p.host > cur.host:
            last[p.call] = p
    own_pos = last.get(own_call) if own_call else None
    rows: list[dict[str, Any]] = []
    for call, p in sorted(last.items()):
        dist = None
        if own_pos is not None and call != own_call:
            dist = round(haversine_km(own_pos.lat, own_pos.lon, p.lat, p.lon), 3)
        rows.append(
            {
                "rufzeichen": call,
                "lat": p.lat,
                "lon": p.lon,
                "mesh": bool(p.mesh),
                "hw": p.hw,
                "letzte_sichtung": fmt_dt(p.host),
                "entfernung_km": dist,
            }
        )
    return {
        "eigener_rufzeichen": own_call,
        "eigene_position_bekannt": own_pos is not None,
        "knoten": rows,
    }


def a10_symmetrie(state: NbrState) -> dict[str, Any]:
    """Abschnitt 10 -- ``[NBR]|SYM`` (docs/nbr-logformat.md, `--nbrsym`).

    Nur geloggte ANGEWENDETE Annahmen (die Firmware schreibt ``SYM`` mit den
    Rollen ``HASF``/``ALT``/``COVER`` ausschliesslich, wenn die Annahme das
    Ergebnis veraendert hat), also KEINE Rate ueber alle Pruefungen -- eine
    hohe Zahl zeigt, wie oft sich die Stufe-2-Relay-Entscheidung tatsaechlich
    auf den Symmetrie-Fallback stuetzt statt auf eine beobachtete Kante.

    Die Rolle ``VETO`` ist das Gegenteil (eine Annahme, die BLOCKIERT wurde)
    und gehoert nicht hierher, sondern zu Abschnitt 11 (HN-Nachbarschafts-
    meldungen) -- sie wird hier herausgefiltert.
    """
    syms = [s for s in state.syms if s.role != "VETO"]
    pro_rolle: Counter = Counter(s.role for s in syms)
    by_pair: dict[tuple[str, str], list[int]] = defaultdict(list)
    for s in syms:
        by_pair[(s.x, s.m)].append(s.snr)
    paare = [
        {
            "x": x,
            "m": m,
            "anzahl": len(snrs),
            "snr_median": round(statistics.median(snrs), 1),
        }
        for (x, m), snrs in sorted(by_pair.items())
    ]
    paare.sort(key=lambda d: -d["anzahl"])
    return {
        "anzahl_gesamt": len(syms),
        "pro_rolle": dict(sorted(pro_rolle.items())),
        "je_paar": paare,
    }


def a11_hn_berichte(state: NbrState) -> dict[str, Any]:
    """Abschnitt 11 -- HN-Nachbarschaftsmeldung (docs/nbr-logformat.md,
    ``--nbrreport``, ``[NBR]|RPT``/``RPTSUM``/``RPTTX``, ``[NBR]|SYM|...|VETO``,
    ``[NBR]|DROP|...|RPT``).

    Vier unabhaengige Quellen, die dieselbe Sache aus vier Seiten zeigen:

    - ``RPTSUM`` -- je EMPFANGENEM Bericht, mit ``<k>`` (Anzahl gelisteter
      Stationen) und ob er vollstaendig war (kein ``+``) oder gekuerzt.
    - ``RPT`` -- je Eintrag DARIN, was daraus wurde (Kante eingetragen /
      ich selbst / keine Zeile). Nur ``ok`` zaehlt als angewendete Kante.
    - ``RPTTX`` -- je SELBST gesendetem Bericht.
    - ``SYM``-Rolle ``VETO`` -- je BLOCKIERTER ``--nbrsym``-Annahme, weil ein
      frischer vollstaendiger Bericht ihr widersprach.
    """
    by_sender: dict[str, list[RptSumRec]] = defaultdict(list)
    for r in state.rptsums:
        by_sender[r.x].append(r)
    berichte_je_sender = [
        {
            "rufzeichen": x,
            "anzahl": len(recs),
            "vollstaendig": sum(1 for r in recs if r.full),
            "gekuerzt": sum(1 for r in recs if not r.full),
            "k_median": round(statistics.median([r.k for r in recs]), 1),
            "heard_median": round(statistics.median([r.heard for r in recs]), 1),
            "applied_gesamt": sum(r.applied for r in recs),
        }
        for x, recs in sorted(by_sender.items())
    ]
    berichte_je_sender.sort(key=lambda d: -d["anzahl"])

    status_je_sender: dict[str, Counter] = defaultdict(Counter)
    for r in state.rpts:
        status_je_sender[r.x][r.status] += 1
    status_rows = [
        {
            "rufzeichen": x,
            "ok": c.get("ok", 0),
            "self": c.get("self", 0),
            "norow": c.get("norow", 0),
        }
        for x, c in sorted(status_je_sender.items())
    ]

    by_edge: dict[tuple[str, str], list[int]] = defaultdict(list)
    for r in state.rpts:
        if r.status == "ok":
            by_edge[(r.x, r.m)].append(r.snr)
    kanten = [
        {"x": x, "m": m, "anzahl": len(snrs), "snr_median": round(statistics.median(snrs), 1)}
        for (x, m), snrs in sorted(by_edge.items())
    ]
    kanten.sort(key=lambda d: -d["anzahl"])

    self_gesamt = sum(1 for r in state.rpts if r.status == "self")
    norow_gesamt = sum(1 for r in state.rpts if r.status == "norow")

    laengen = [r.length for r in state.rpttx]
    eigene = {
        "anzahl": len(state.rpttx),
        "laenge_median": round(statistics.median(laengen), 1) if laengen else None,
        "laenge_min": min(laengen) if laengen else None,
        "laenge_max": max(laengen) if laengen else None,
    }

    veto_by_pair: dict[tuple[str, str], list[int]] = defaultdict(list)
    for s in state.syms:
        if s.role == "VETO":
            veto_by_pair[(s.x, s.m)].append(s.snr)
    veto = [
        {"x": x, "m": m, "anzahl": len(snrs), "snr_median": round(statistics.median(snrs), 1)}
        for (x, m), snrs in sorted(veto_by_pair.items())
    ]
    veto.sort(key=lambda d: -d["anzahl"])
    veto_gesamt = sum(v["anzahl"] for v in veto)

    rpt_malformed = sum(1 for d in state.drops if d.reason == "RPT")

    return {
        "berichte_je_sender": berichte_je_sender,
        "status_je_sender": status_rows,
        "kanten_angewendet": kanten,
        "self_gesamt": self_gesamt,
        "norow_gesamt": norow_gesamt,
        "eigene_berichte": eigene,
        "veto_je_paar": veto,
        "veto_gesamt": veto_gesamt,
        "rpt_malformed_gesamt": rpt_malformed,
    }


def analyze(state: NbrState) -> dict[str, Any]:
    own_call = own_call_of(state)
    neighbours = direct_neighbours(state)
    heard = heard_sets(state, neighbours)
    # Fuer Abschnitt 4/4b (Fund 2b): der eigene Call darf in keiner gehoerten
    # Menge auftauchen. Ohne bekannten eigenen Call (keine SNAP-Zeile im
    # Mitschnitt) wird NICHT geraten -- die Filterung faellt dann einfach aus.
    heard_for_urteil = heard_sets(state, neighbours, exclude=own_call)
    own_heard = set(neighbours)
    urteil = a4_urteil(state, neighbours, heard_for_urteil, own_heard)
    return {
        "meta": {
            "generated_by": "tools/nbrlog.py",
            "format_vertrag": "docs/nbr-logformat.md",
        },
        "1_rahmen": a1_rahmen(state),
        "eigener_rufzeichen": own_call,
        "eigener_rufzeichen_bekannt": own_call is not None,
        "2_nachbarschaft": a2_nachbarschaft(state),
        "3_kreuzmatrix": a3_kreuzmatrix(state, neighbours, heard),
        "4_urteil": urteil,
        "4b_deckung": a4b_deckung(neighbours, heard_for_urteil, own_heard, urteil["je_nachbar"]),
        "5_stabilitaet": a5_stabilitaet(state, neighbours),
        "6_tabellendruck": a6_tabellendruck(state),
        "7_zwei_hop_schnitt": a7_zwei_hop_schnitt(state),
        "8_verworfene_frames": a8_verworfene_frames(state),
        "9_positionen": a9_positionen(state, own_call),
        "10_symmetrie": a10_symmetrie(state),
        "11_hn_berichte": a11_hn_berichte(state),
    }


# --------------------------------------------------------------------------
# --fetch -- Logs per rsync/scp von einem Pi holen
# --------------------------------------------------------------------------


def fetch_spec_to_parts(spec: str) -> tuple[str, str, str]:
    """Zerlegt "[user@]host:pfad" in (user_at_host, host, remote_pfad)."""
    if ":" not in spec:
        raise SystemExit(
            f"--fetch erwartet die Form [user@]host:pfad, bekommen: {spec!r}"
        )
    user_at_host, remote_path = spec.split(":", 1)
    if not user_at_host or not remote_path:
        raise SystemExit(f"--fetch: host oder pfad fehlt in {spec!r}")
    host = user_at_host.split("@", 1)[-1]
    return user_at_host, host, remote_path


def build_fetch_commands(spec: str, out_dir: Path) -> tuple[list[str], list[str]]:
    """Liefert (rsync_kommando, scp_kommando) -- rsync ist die erste Wahl."""
    user_at_host, _host, remote_path = fetch_spec_to_parts(spec)
    remote = remote_path if remote_path.endswith("/") else remote_path + "/"
    rsync_cmd = ["rsync", "-az", f"{user_at_host}:{remote}", str(out_dir) + "/"]
    scp_cmd = ["scp", "-r", f"{user_at_host}:{remote_path.rstrip('/')}", str(out_dir)]
    return rsync_cmd, scp_cmd


def do_fetch(spec: str, out_dir: Path, dry_run: bool) -> Path:
    """Holt die Logs nach ``out_dir``. Bei ``dry_run`` wird nur der Befehl gezeigt."""
    _user_at_host, host, _remote = fetch_spec_to_parts(spec)
    out_dir.mkdir(parents=True, exist_ok=True)
    rsync_cmd, scp_cmd = build_fetch_commands(spec, out_dir)
    use_rsync = shutil.which("rsync") is not None
    cmd = rsync_cmd if use_rsync else scp_cmd
    printable = " ".join(shlex.quote(c) for c in cmd)

    if dry_run:
        print("Trockenlauf -- es wird NICHTS abgerufen. Folgender Befehl wuerde laufen:")
        print(f"  {printable}")
        if not use_rsync:
            print("  (rsync nicht gefunden -- Fallback auf scp)")
        return out_dir

    print(f"hole Logs von {host}: {printable}")
    result = subprocess.run(cmd, check=False)
    if result.returncode != 0:
        raise SystemExit(f"Abruf fehlgeschlagen (rc={result.returncode}): {printable}")
    return out_dir


def default_fetch_workdir(spec: str) -> Path:
    _user_at_host, host, _remote = fetch_spec_to_parts(spec)
    return Path.home() / "Downloads" / f"nbrlog-{host}"


def collect_log_files(directory: Path) -> list[Path]:
    files = [p for p in directory.iterdir() if p.is_file() and (p.suffix in (".log", ".gz"))]
    return sorted(files, key=lambda p: p.name)


# --------------------------------------------------------------------------
# Markdown-Bericht
# --------------------------------------------------------------------------


def _md_table(headers: list[str], rows: list[list[Any]]) -> str:
    if not rows:
        return "_keine Daten_\n"
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"]
    for r in rows:
        lines.append("| " + " | ".join("" if v is None else str(v) for v in r) + " |")
    return "\n".join(lines) + "\n"


def render_bluf(res: dict[str, Any]) -> list[str]:
    rahmen = res["1_rahmen"]
    urteil = res["4_urteil"]
    druck = res["6_tabellendruck"]
    lines = ["## BLUF", ""]

    n_excl = sum(1 for r in urteil["je_nachbar"] if r["eigenes_urteil"] == "exklusiv")
    n_red = sum(1 for r in urteil["je_nachbar"] if r["eigenes_urteil"] == "redundant")
    n_neighbours = len(urteil["je_nachbar"])
    n_abw = len(urteil["abweichungen"])
    n_nv = len(urteil.get("nicht_vergleichbar", []))

    lines.append(
        f"- Zeitraum {rahmen['von']} bis {rahmen['bis']}, "
        f"{n_neighbours} direkte Nachbarn: {n_excl} eigenes Urteil **exklusiv** "
        f"(muessen selbst meshen, == Firmware-`MESH`), {n_red} **redundant** (== `RED`)."
    )
    if n_abw:
        lines.append(
            f"- **{n_abw} Abweichung(en)** zwischen eigenem Urteil und "
            "Firmware-`<meshneed>` -- Tabelle in Abschnitt 4, das ist das "
            "interessanteste Ergebnis des Tests."
        )
    elif n_abw == 0 and n_nv == n_neighbours and n_neighbours:
        lines.append(
            "- **Kein Vergleich moeglich**: fuer keinen Nachbarn lag ein Firmware-`<meshneed>` "
            "vor (kein Snapshot, oder ein Mitschnitt aus der Zeit vor dem Feld). Das eigene "
            "Urteil oben steht, aber es ist durch nichts gegengeprueft."
        )
    else:
        lines.append(
            "- Eigenes Urteil und Firmware-`<meshneed>` stimmen ueberall dort ueberein, wo "
            f"verglichen werden konnte ({n_neighbours - n_nv} von {n_neighbours} Nachbarn)."
        )
    if n_nv and n_abw:
        lines.append(
            f"- {n_nv} Nachbar(n) waren nicht vergleichbar (kein Firmware-Urteil vorhanden) -- "
            "das ist kein Widerspruch, siehe Abschnitt 4."
        )

    deckung = res["4b_deckung"]
    gruppen = deckung["wechselseitig_redundante_gruppen"]
    if gruppen:
        gruppen_txt = "; ".join(
            f"{{{', '.join(g['nachbarn'])}}} (gemeinsam: {', '.join(g['gemeinsame_knoten'])})"
            for g in gruppen
        )
        lines.append(
            f"- **ACHTUNG: {len(gruppen)} wechselseitig redundante Gruppe(n)** -- "
            f"einzeln als redundant markiert, aber nicht alle gleichzeitig abschaltbar: {gruppen_txt}. "
            "Siehe Abschnitt 4b, das ist eine Warnung, kein Freibrief."
        )

    if druck["ueberlauf_erkannt"]:
        lines.append(
            f"- **ACHTUNG: Tabellenueberlauf erkannt** ({len(druck['ueberlauf_snapshots'])} "
            "Snapshot(s) mit rows == maxrows) -- das Urteil aus Abschnitt 4 ist fuer diese "
            "Zeitpunkte NICHT haltbar, weil die Matrix nicht mehr alle 2-Hop-Nachbarn hielt."
        )
    if rahmen["reboots"]:
        lines.append(f"- {rahmen['reboots']} Reboot(s) im Mitschnitt erkannt (Sitzung dort getrennt).")
    if rahmen["anzahl_luecken"]:
        lines.append(f"- {rahmen['anzahl_luecken']} Mitschnitt-Luecke(n) > 2 min.")
    if druck["evict_gesamt"]:
        lines.append(f"- {druck['evict_gesamt']} EVICT-Ereignisse insgesamt -- Tabellendruck ist real.")
    if res["8_verworfene_frames"]["full_alarm"]:
        lines.append(
            f"- **{res['8_verworfene_frames']['full_anzahl']} DROP|FULL** -- Alarmsignal, "
            "die Matrix war voll und hat Frames verworfen statt sie einzutragen."
        )
    n_sym = res["10_symmetrie"]["anzahl_gesamt"]
    if n_sym:
        lines.append(
            f"- {n_sym} `--nbrsym`-Symmetrie-Annahme(n) haben eine Stufe-2-Relay-Entscheidung "
            "veraendert -- Aufschluesselung in Abschnitt 10."
        )
    hn = res["11_hn_berichte"]
    if hn["veto_gesamt"]:
        lines.append(
            f"- {hn['veto_gesamt']} `--nbrsym`-Annahme(n) durch einen frischen vollstaendigen "
            "HN-Bericht blockiert (VETO) -- Abschnitt 11."
        )
    if hn["rpt_malformed_gesamt"]:
        lines.append(
            f"- **{hn['rpt_malformed_gesamt']}x DROP|RPT** -- fehlerhafte HN-Berichte, "
            "nichts daraus angewendet."
        )
    lines.append("")
    return lines


def render_md(res: dict[str, Any]) -> str:
    out: list[str] = ["# NBR-Dauertest -- Auswertung", ""]
    out += render_bluf(res)

    rahmen = res["1_rahmen"]
    out.append("## 1. Rahmen")
    out.append("")
    out.append(f"- Zeitraum: {rahmen['von']} bis {rahmen['bis']}")
    out.append(f"- Ausgewertete Dateien: {', '.join(rahmen['dateien']) or '(keine)'}")
    out.append(f"- Zeilen gesamt: {rahmen['zeilen_gesamt']}")
    out.append(f"- [NBR]-Zeilen: {rahmen['nbr_zeilen']}")
    out.append(f"- Verworfene Zeilen: {rahmen['verworfen_gesamt']}")
    out.append(f"- Reboots: {rahmen['reboots']}")
    out.append(f"- Mitschnitt-Luecken (> 2 min): {rahmen['anzahl_luecken']}")
    out.append(f"- Eigenes Rufzeichen (aus SNAP): {res['eigener_rufzeichen']}")
    out.append("")
    out.append("Verworfen nach Grund:")
    out.append("")
    out.append(
        _md_table(
            ["Grund", "Anzahl"],
            [[k, v] for k, v in rahmen["verworfen_gruende"].items()],
        )
    )
    if rahmen["reboot_ereignisse"]:
        out.append("Reboot-Ereignisse:")
        out.append("")
        out.append(
            _md_table(
                ["Zeitpunkt", "up vorher", "up nachher"],
                [[e["host"], e["up_vorher"], e["up_nachher"]] for e in rahmen["reboot_ereignisse"]],
            )
        )
    if rahmen["luecken"]:
        out.append("Luecken:")
        out.append("")
        out.append(
            _md_table(
                ["von", "bis", "Dauer (s)"],
                [[g["von"], g["bis"], g["dauer_s"]] for g in rahmen["luecken"]],
            )
        )

    nb = res["2_nachbarschaft"]
    out.append("## 2. Nachbarschaft")
    out.append("")
    out.append(f"{nb['anzahl_direkte_nachbarn']} direkte Nachbarn (Quelle: ME-Zeilen).")
    out.append("")
    out.append(
        _md_table(
            [
                "Rufzeichen", "Anzahl", "Typen",
                "RSSI median", "RSSI min", "RSSI max", "RSSI ohne Bericht",
                "SNR median", "SNR min", "SNR max", "SNR ohne Bericht",
                "erste Sichtung", "letzte Sichtung",
            ],
            [
                [
                    r["rufzeichen"], r["anzahl"], r["typverteilung"],
                    r["rssi_median"], r["rssi_min"], r["rssi_max"], r["rssi_ohne_bericht"],
                    r["snr_median"], r["snr_min"], r["snr_max"], r["snr_ohne_bericht"],
                    r["erste_sichtung"], r["letzte_sichtung"],
                ]
                for r in nb["je_nachbar"]
            ],
        )
    )

    km = res["3_kreuzmatrix"]
    out.append("## 3. Kreuzmatrix")
    out.append("")
    out.append("Je direktem Nachbar, was er gehoert hat:")
    out.append("")
    out.append(
        _md_table(
            ["Nachbar", "Anzahl gehoert", "gehoert"],
            [[r["rufzeichen"], r["anzahl_gehoert"], ", ".join(r["gehoert"]) or "(nichts)"] for r in km["je_nachbar"]],
        )
    )
    out.append("Umgekehrt, wer diesen Knoten gehoert hat:")
    out.append("")
    out.append(
        _md_table(
            ["Knoten", "gehoert von"],
            [[r["rufzeichen"], ", ".join(r["gehoert_von"])] for r in km["umgekehrt"]],
        )
    )

    urt = res["4_urteil"]
    out.append("## 4. Urteil: muss der Nachbar selbst meshen?")
    out.append("")
    out.append(
        "Zwei Spalten, zwei entgegengesetzte Fragen an dieselbe Kante "
        "(docs/nbr-logformat.md) -- sie duerfen nicht verwechselt werden: "
        "**Firmware-`<verdict>`** fragt, ob AUSSER MIR jemand diesen Nachbarn "
        "hoert (Sicht: Nachbar als Gehoerter); **eigenes Urteil / "
        "Firmware-`<meshneed>`** fragt, ob DIESER Nachbar Knoten hoert, die "
        "sonst niemand hoert (Sicht: Nachbar als Hoerer). Nur die zweite "
        "Spalte wird unten verglichen, `<verdict>` steht nur zur Information "
        "daneben."
    )
    if not res["eigener_rufzeichen_bekannt"]:
        out.append("")
        out.append(
            "**Eigener Rufname unbekannt** (keine SNAP-Zeile im Mitschnitt) -- "
            "die Fund-2b-Filterung (eigener Call raus aus jeder gehoerten "
            "Menge) konnte nicht angewendet werden, das Urteil unten ist damit "
            "mit Vorsicht zu lesen."
        )
    out.append("")
    out.append(
        _md_table(
            ["Rufzeichen", "eigenes Urteil", "unabgedeckte Knoten", "Firmware-`<meshneed>`", "Uebereinstimmung", "Firmware-`<verdict>` (nur Info)"],
            [
                [
                    r["rufzeichen"], r["eigenes_urteil"], ", ".join(r["exklusive_knoten"]) or "-",
                    r["firmware_meshneed"] or "(nicht in Tabelle)",
                    "ja" if r["uebereinstimmung"] is True else ("nein" if r["uebereinstimmung"] is False else "n/a"),
                    r["firmware_verdict"] or "(nicht in Tabelle)",
                ]
                for r in urt["je_nachbar"]
            ],
        )
    )
    out.append("")
    out.append("### Abweichungen eigenes Urteil <-> Firmware-`<meshneed>`")
    out.append("")
    if urt["abweichungen"]:
        out.append(
            _md_table(
                ["Rufzeichen", "eigenes Urteil", "Firmware-`<meshneed>`"],
                [[r["rufzeichen"], r["eigenes_urteil"], r["firmware_meshneed"] or "(nicht in Tabelle)"] for r in urt["abweichungen"]],
            )
        )
    else:
        out.append("Keine.\n")

    out.append("### Nicht vergleichbar")
    out.append("")
    if urt.get("nicht_vergleichbar"):
        out.append(
            "Kein Widerspruch, sondern nichts zum Vergleichen: kein Snapshot, ein Mitschnitt "
            "aus der Zeit vor dem `<meshneed>`-Feld, oder der Nachbar stand zum "
            "Snapshot-Zeitpunkt gar nicht in der Matrix. Der letzte Fall ist Tabellendruck "
            "und gehoert zu Abschnitt 6.\n"
        )
        out.append(
            _md_table(
                ["Rufzeichen", "eigenes Urteil", "Firmware-`<meshneed>`"],
                [[r["rufzeichen"], r["eigenes_urteil"],
                  r["firmware_meshneed"] or "(nicht in Tabelle)"] for r in urt["nicht_vergleichbar"]],
            )
        )
    else:
        out.append("Keine.\n")

    deck = res["4b_deckung"]
    out.append("## 4b. Deckungsmenge und wechselseitig redundante Gruppen")
    out.append("")
    out.append(
        "Die paarweise Rechnung aus Abschnitt 4 beantwortet nur \"ist DIESER "
        "eine Nachbar verzichtbar, wenn alle anderen bleiben?\". Hoeren zwei "
        "Nachbarn exakt dieselbe (sonst von niemandem gehoerte) Menge, gilt "
        "in dieser Rechnung jeder fuer sich als redundant -- schaltet man "
        "aber beide ab, fehlt die Menge. Dieser Abschnitt macht das explizit."
    )
    out.append("")
    out.append(
        f"- Universum (von irgendeinem Nachbarn gehoert, von mir nicht "
        f"direkt): {deck['universum_groesse']} Knoten."
    )
    out.append(
        f"- Minimale Deckungsmenge (Greedy, **nicht beweisbar minimal** -- "
        f"Set Cover ist NP-schwer, das ist eine obere Schranke): "
        f"{', '.join(deck['deckungsmenge']) or '(keine noetig)'} "
        f"({deck['deckungsmenge_groesse']} Nachbar(n))."
    )
    out.append("")
    out.append("### Wechselseitig redundante Gruppen")
    out.append("")
    if deck["wechselseitig_redundante_gruppen"]:
        out.append(
            "**Warnung, kein Freibrief:** jeder Nachbar in einer Gruppe gilt "
            "einzeln als redundant, aber nicht alle gleichzeitig abschaltbar "
            "-- mindestens einer muss bleiben, sonst fehlen die gemeinsamen Knoten."
        )
        out.append("")
        out.append(
            _md_table(
                ["Nachbarn", "gemeinsame Knoten (Grund der Abhaengigkeit)"],
                [
                    [", ".join(g["nachbarn"]), ", ".join(g["gemeinsame_knoten"])]
                    for g in deck["wechselseitig_redundante_gruppen"]
                ],
            )
        )
    else:
        out.append("Keine.\n")

    stab = res["5_stabilitaet"]
    out.append("## 5. Stabilitaet des Urteils ueber die Zeit")
    out.append("")
    out.append(
        _md_table(
            ["Rufzeichen", "Verlauf", "Anzahl Snapshots", "stabil ab Snapshot", "letztes Urteil"],
            [
                [r["rufzeichen"], " -> ".join(r["verlauf"]) or "(keine Snapshot-Daten)", r["anzahl_snapshots"], r["stabil_ab_snapshot"], r["letztes_urteil"]]
                for r in stab["je_nachbar"]
            ],
        )
    )

    dr = res["6_tabellendruck"]
    out.append("## 6. Tabellendruck")
    out.append("")
    out.append(f"- rows max: {dr['rows_max']}, rows median: {dr['rows_median']}")
    out.append(f"- EVICT gesamt: {dr['evict_gesamt']}")
    if dr["ueberlauf_erkannt"]:
        out.append(f"- **Tabellenueberlauf in {len(dr['ueberlauf_snapshots'])} Snapshot(s)** -- Urteil aus Abschnitt 4 dort nicht haltbar.")
    out.append("")
    out.append(
        _md_table(
            ["Zeitpunkt", "up", "rows", "maxrows", "voll"],
            [[r["host"], r["up"], r["rows"], r["maxrows"], "JA" if r["voll"] else ""] for r in dr["reihen"]],
        )
    )
    out.append("EVICT je Stunde:")
    out.append("")
    out.append(_md_table(["Stunde", "Anzahl"], [[k, v] for k, v in dr["evict_pro_stunde"].items()]))
    out.append("Verdraengte Rufzeichen:")
    out.append("")
    out.append(_md_table(["Rufzeichen", "Anzahl verdraengt"], [[k, v] for k, v in dr["verdraengte_rufzeichen"].items()]))

    cut = res["7_zwei_hop_schnitt"]
    out.append("## 7. Wirkung des 2-Hop-Schnitts")
    out.append("")
    out.append(f"- Frames gekuerzt: {cut['frames_gekuerzt']}")
    out.append(f"- Pfad-Token insgesamt verworfen: {cut['token_verworfen_gesamt']}")
    out.append("")
    out.append("Verteilung nach ursprueglicher Pfadlaenge (`<ntok>`):")
    out.append("")
    out.append(_md_table(["ntok", "Anzahl"], [[k, v] for k, v in cut["verteilung_ntok"].items()]))

    dp = res["8_verworfene_frames"]
    out.append("## 8. Verworfene Frames (DROP)")
    out.append("")
    if dp["full_alarm"]:
        out.append(f"**ALARM: {dp['full_anzahl']}x DROP|FULL -- die Tabelle war voll.**")
        out.append("")
    out.append(_md_table(["Grund", "Anzahl"], [[k, v] for k, v in dp["pro_grund"].items()]))
    out.append("Je Stunde und Grund:")
    out.append("")
    hour_reasons = sorted({r for c in dp["pro_stunde_und_grund"].values() for r in c})
    out.append(
        _md_table(
            ["Stunde", *hour_reasons],
            [[h, *[c.get(r, 0) for r in hour_reasons]] for h, c in dp["pro_stunde_und_grund"].items()],
        )
    )

    pos = res["9_positionen"]
    out.append("## 9. Positionen")
    out.append("")
    if not pos["eigene_position_bekannt"]:
        out.append("Eigene Position nicht im Log gefunden -- Reichweiten nicht berechenbar.")
        out.append("")
    out.append(
        _md_table(
            ["Rufzeichen", "lat", "lon", "mesh", "hw", "letzte Sichtung", "Entfernung (km)"],
            [
                [r["rufzeichen"], r["lat"], r["lon"], "ja" if r["mesh"] else "nein", r["hw"], r["letzte_sichtung"], r["entfernung_km"]]
                for r in pos["knoten"]
            ],
        )
    )

    sym = res["10_symmetrie"]
    out.append("## 10. Symmetrie-Annahmen (SYM, `--nbrsym`)")
    out.append("")
    out.append(
        "Nur geloggte Annahmen, die das Ergebnis einer Stufe-2-Relay-Entscheidung "
        "tatsaechlich veraendert haben (docs/nbr-logformat.md) -- keine Zaehlung "
        "aller Pruefungen. `HASF`: X gilt als schon im Besitz des Frames, weil es "
        "M hoert. `ALT`: X gilt nicht als alleiniger Traeger, weil ein Versorger M "
        "angenommen wird. `COVER`: X gilt durch die gehoerte Wiederholung von M als "
        "abgedeckt."
    )
    out.append("")
    out.append(f"- Annahmen insgesamt: {sym['anzahl_gesamt']}")
    out.append("")
    out.append("Je Rolle:")
    out.append("")
    out.append(_md_table(["Rolle", "Anzahl"], [[k, v] for k, v in sym["pro_rolle"].items()]))
    out.append("Je Paar (x hoert m angenommen):")
    out.append("")
    out.append(
        _md_table(
            ["x", "m", "Anzahl", "SNR median"],
            [[p["x"], p["m"], p["anzahl"], p["snr_median"]] for p in sym["je_paar"]],
        )
    )

    hn = res["11_hn_berichte"]
    out.append("## 11. HN-Nachbarschaftsmeldungen (`--nbrreport`)")
    out.append("")
    out.append(
        "Vier Quellen (docs/nbr-logformat.md): `RPTSUM` je empfangenem Bericht, `RPT` je "
        "Eintrag darin (nur `ok` ist eine angewendete Kante), `RPTTX` je selbst gesendetem "
        "Bericht, `SYM`-Rolle `VETO` je durch einen frischen vollstaendigen Bericht "
        "blockierter `--nbrsym`-Annahme."
    )
    out.append("")
    out.append("### Empfangene Berichte je Sender")
    out.append("")
    out.append(
        _md_table(
            ["Sender x", "Anzahl", "vollstaendig", "gekuerzt (+)", "k median", "heard median", "Kanten angewendet"],
            [
                [
                    r["rufzeichen"], r["anzahl"], r["vollstaendig"], r["gekuerzt"],
                    r["k_median"], r["heard_median"], r["applied_gesamt"],
                ]
                for r in hn["berichte_je_sender"]
            ],
        )
    )
    out.append("### Status je Eintrag, je Sender")
    out.append("")
    out.append(
        _md_table(
            ["Sender x", "ok", "self", "norow"],
            [[r["rufzeichen"], r["ok"], r["self"], r["norow"]] for r in hn["status_je_sender"]],
        )
    )
    out.append(f"- `self` gesamt (x hat MICH gehoert): {hn['self_gesamt']}")
    out.append(f"- `norow` gesamt (m ohne Matrixzeile, ignoriert): {hn['norow_gesamt']}")
    out.append("")
    out.append("### Angewendete Kanten (x hoert m, aus HN-Bericht)")
    out.append("")
    out.append(
        _md_table(
            ["x", "m", "Anzahl", "SNR median"],
            [[k["x"], k["m"], k["anzahl"], k["snr_median"]] for k in hn["kanten_angewendet"]],
        )
    )
    eigene = hn["eigene_berichte"]
    out.append("### Selbst gesendete Berichte (RPTTX)")
    out.append("")
    out.append(
        f"- Anzahl: {eigene['anzahl']}, Laenge median: {eigene['laenge_median']}, "
        f"min: {eigene['laenge_min']}, max: {eigene['laenge_max']}"
    )
    out.append("")
    out.append("### VETO -- blockierte `--nbrsym`-Annahmen")
    out.append("")
    out.append(f"- Insgesamt: {hn['veto_gesamt']}")
    out.append("")
    out.append(
        _md_table(
            ["x", "m", "Anzahl", "SNR median"],
            [[v["x"], v["m"], v["anzahl"], v["snr_median"]] for v in hn["veto_je_paar"]],
        )
    )
    if hn["rpt_malformed_gesamt"]:
        out.append(
            f"**{hn['rpt_malformed_gesamt']}x DROP|RPT** -- fehlerhafte HN-Berichte verworfen, "
            "nichts angewendet.\n"
        )

    return "\n".join(out) + "\n"


def maybe_prettify(path: Path) -> None:
    if shutil.which("npx") is None:
        print(f"WARNUNG: npx nicht gefunden -- {path} nicht formatiert", file=sys.stderr)
        return
    try:
        subprocess.run(
            ["npx", "--yes", "prettier@3", "--write", str(path)],
            check=True, capture_output=True,
        )
    except (subprocess.CalledProcessError, OSError) as exc:
        print(f"WARNUNG: prettier auf {path} fehlgeschlagen: {exc}", file=sys.stderr)


# --------------------------------------------------------------------------
# Selbsttest -- laeuft ohne Netz gegen die eingebauten Fixtures
# --------------------------------------------------------------------------

TESTDATA_DIR = Path(__file__).resolve().parent / "testdata" / "nbr"


def _check(label: str, got: Any, want: Any, failures: list[str]) -> None:
    if got != want:
        failures.append(f"{label}: bekommen {got!r}, erwartet {want!r}")


def run_self_test() -> int:
    failures: list[str] = []

    # -- 1) realistischer 24h-Auszug: alle Zeilentypen, ein Reboot, eine
    #    Mitschnitt-Luecke, ein FULL-Drop, mehrere EVICTs, zwei Snapshots --
    state = parse_files([TESTDATA_DIR / "nbr_sample_24h.log"])
    res = analyze(state)
    r = res["1_rahmen"]
    _check("24h zeilen_gesamt", r["zeilen_gesamt"], 54, failures)
    _check("24h nbr_zeilen", r["nbr_zeilen"], 46, failures)
    _check("24h verworfen_gesamt", r["verworfen_gesamt"], 8, failures)
    _check("24h verworfen_gruende", r["verworfen_gruende"], {"foreign_line": 8}, failures)
    _check("24h reboots", r["reboots"], 1, failures)
    _check("24h anzahl_luecken", r["anzahl_luecken"], 2, failures)
    _check("24h eigener_rufzeichen", res["eigener_rufzeichen"], "DK5EN-98", failures)

    nb = {row["rufzeichen"]: row for row in res["2_nachbarschaft"]["je_nachbar"]}
    _check("24h anzahl_nachbarn", res["2_nachbarschaft"]["anzahl_direkte_nachbarn"], 3, failures)
    _check("24h ME93 anzahl", nb["DK5EN-93"]["anzahl"], 4, failures)
    _check("24h ME93 rssi_min", nb["DK5EN-93"]["rssi_min"], -91, failures)
    _check("24h ME93 rssi_max", nb["DK5EN-93"]["rssi_max"], -83, failures)
    _check("24h ME93 rssi_median", nb["DK5EN-93"]["rssi_median"], -87.5, failures)
    _check("24h ME95 anzahl", nb["DK5EN-95"]["anzahl"], 3, failures)
    _check("24h ME95 rssi_median", nb["DK5EN-95"]["rssi_median"], -88, failures)
    _check("24h ME97 anzahl", nb["DK5EN-97"]["anzahl"], 2, failures)
    _check("24h ME97 rssi_ohne_bericht", nb["DK5EN-97"]["rssi_ohne_bericht"], 1, failures)
    _check("24h ME97 rssi_min", nb["DK5EN-97"]["rssi_min"], -95, failures)

    urt = {row["rufzeichen"]: row for row in res["4_urteil"]["je_nachbar"]}
    # Diese Fixture ist komplett im ALTEN ROW-Format (kein <meshneed>) --
    # firmware_meshneed ist deshalb fuer jede Zeile None und uebereinstimmung
    # entsprechend nirgends True. firmware_verdict (informativ, <verdict>)
    # bleibt unabhaengig davon vorhanden, wo die Zeile in einem Snapshot war.
    _check("24h urteil 93", urt["DK5EN-93"]["eigenes_urteil"], "exklusiv", failures)
    _check("24h urteil 93 exkl-knoten", urt["DK5EN-93"]["exklusive_knoten"], ["OE9ZZZ-5"], failures)
    _check("24h urteil 93 firmware_verdict", urt["DK5EN-93"]["firmware_verdict"], "EXCL", failures)
    _check("24h urteil 93 firmware_meshneed", urt["DK5EN-93"]["firmware_meshneed"], None, failures)
    _check("24h urteil 93 match", urt["DK5EN-93"]["uebereinstimmung"], None, failures)
    _check("24h urteil 95", urt["DK5EN-95"]["eigenes_urteil"], "redundant", failures)
    _check("24h urteil 95 firmware_verdict", urt["DK5EN-95"]["firmware_verdict"], "RED", failures)
    _check("24h urteil 95 firmware_meshneed", urt["DK5EN-95"]["firmware_meshneed"], None, failures)
    _check("24h urteil 95 match", urt["DK5EN-95"]["uebereinstimmung"], None, failures)
    _check("24h urteil 97", urt["DK5EN-97"]["eigenes_urteil"], "redundant", failures)
    _check("24h urteil 97 firmware_verdict", urt["DK5EN-97"]["firmware_verdict"], None, failures)
    _check("24h urteil 97 match", urt["DK5EN-97"]["uebereinstimmung"], None, failures)
    # Diese Fixture ist komplett im alten ROW-Format: es gibt nichts zu
    # vergleichen, also NULL Widersprueche und drei nicht vergleichbare Zeilen.
    _check("24h abweichungen", len(res["4_urteil"]["abweichungen"]), 0, failures)
    _check("24h nicht vergleichbar", len(res["4_urteil"]["nicht_vergleichbar"]), 3, failures)

    # Rueckwaerts-Kompatibilitaet: diese Fixture stammt aus der Zeit vor dem
    # <meshneed>-Feld -- jede ROW-Zeile hat nur 6 statt 7 Felder und muss
    # trotzdem sauber parsen, mit meshneed=None statt einem Fehler.
    if state.snaps and state.snaps[0].rows_entries:
        _check("24h alt-format meshneed", state.snaps[0].rows_entries[0].meshneed, None, failures)

    dr = res["6_tabellendruck"]
    _check("24h rows_max", dr["rows_max"], 21, failures)
    _check("24h rows_median", dr["rows_median"], 3, failures)
    _check("24h ueberlauf_erkannt", dr["ueberlauf_erkannt"], True, failures)
    _check("24h evict_gesamt", dr["evict_gesamt"], 3, failures)
    _check("24h evict_pro_stunde", dr["evict_pro_stunde"], {"2026-09-20 03": 3}, failures)
    _check(
        "24h verdraengte_rufzeichen",
        dr["verdraengte_rufzeichen"],
        {"OE2QQQ-1": 2, "OE6FFF-6": 1},
        failures,
    )

    cut = res["7_zwei_hop_schnitt"]
    _check("24h cut frames", cut["frames_gekuerzt"], 2, failures)
    _check("24h cut tokens", cut["token_verworfen_gesamt"], 5, failures)
    _check("24h cut ntok hist", cut["verteilung_ntok"], {4: 1, 5: 1}, failures)

    dp = res["8_verworfene_frames"]
    _check("24h drop pro_grund", dp["pro_grund"], {"FULL": 1, "LOOP": 1, "TOK": 1, "TYPE": 1}, failures)
    _check("24h drop full_alarm", dp["full_alarm"], True, failures)

    pos = {row["rufzeichen"]: row for row in res["9_positionen"]["knoten"]}
    _check("24h pos eigene bekannt", res["9_positionen"]["eigene_position_bekannt"], True, failures)
    _check("24h pos OE1AAA lat", pos["OE1AAA-1"]["lat"], 48.2005, failures)
    dist = pos["OE1AAA-1"]["entfernung_km"]
    if dist is None or not (0.5 < dist < 3.0):
        failures.append(f"24h pos entfernung ausserhalb erwarteter Spanne (0.5..3.0 km): {dist}")

    # -- 2) bewusst kaputter Auszug: abgeschnittene Zeile, Muellzeichen,
    #    unbekannter Untertyp, Zeile ohne Zeitstempel, verschweisste Zeile --
    state_c = parse_files([TESTDATA_DIR / "nbr_sample_corrupt.log"])
    res_c = analyze(state_c)
    rc = res_c["1_rahmen"]
    _check("corrupt zeilen_gesamt", rc["zeilen_gesamt"], 14, failures)
    _check("corrupt nbr_zeilen", rc["nbr_zeilen"], 6, failures)
    _check("corrupt verworfen_gesamt", rc["verworfen_gesamt"], 8, failures)
    want_reasons = {
        "foreign_line": 1,
        "unknown_subtype:FOO": 1,
        "malformed:ME": 1,
        "no_timestamp": 1,
        "garbled_prefix": 1,
        "glued_line": 1,
        "row_without_snap": 1,
        "malformed:POS": 1,
    }
    _check("corrupt verworfen_gruende", rc["verworfen_gruende"], want_reasons, failures)
    _check("corrupt eigener_rufzeichen", res_c["eigener_rufzeichen"], "DK5EN-98", failures)
    _check("corrupt snap count", len(state_c.snaps), 1, failures)
    if state_c.snaps:
        _check("corrupt snap rows_entries", len(state_c.snaps[0].rows_entries), 2, failures)

    # -- 3) handkonstruiertes Beispiel fuer Abschnitt 4 (Betreiberfrage
    #    MESH/RED gegen Firmware-<meshneed>) und 4b (Deckungsmenge /
    #    wechselseitig redundante Gruppen). Die vollstaendige Handrechnung
    #    steht als Kommentarkopf in nbr_sample_verdict.log; hier nur die
    #    Werte, die daraus folgen.
    state_v = parse_files([TESTDATA_DIR / "nbr_sample_verdict.log"])
    res_v = analyze(state_v)

    # Rueckwaerts-Kompatibilitaet (Fund 1): der erste (aeltere) Snapshot hat
    # keine <meshneed>-Spalte -- muss sauber als None durchgehen, kein Fehler.
    _check("verdict snap count", len(state_v.snaps), 2, failures)
    if len(state_v.snaps) >= 1 and len(state_v.snaps[0].rows_entries) >= 2:
        old_row = state_v.snaps[0].rows_entries[1]
        _check("verdict alt-format call", old_row.call, "DK5EN-93", failures)
        _check("verdict alt-format meshneed", old_row.meshneed, None, failures)
        _check("verdict alt-format verdict", old_row.verdict, "EXCL", failures)

    # NA: OE1AAA-1 ist kein direkter Nachbar (keine ME-Zeile) -- meshneed=NA
    # muss unveraendert durchgereicht werden und darf nicht in Abschnitt 4
    # (nur direkte Nachbarn) auftauchen.
    if len(state_v.snaps) >= 2:
        new_rows = {r.call: r for r in state_v.snaps[1].rows_entries}
        _check("verdict OE1AAA-1 meshneed", new_rows["OE1AAA-1"].meshneed, "NA", failures)
        _check("verdict OE1AAA-1 verdict", new_rows["OE1AAA-1"].verdict, "RED", failures)

    urt_v = {row["rufzeichen"]: row for row in res_v["4_urteil"]["je_nachbar"]}
    _check("verdict anzahl nachbarn", len(urt_v), 6, failures)
    _check("verdict OE1AAA-1 nicht in urteil", "OE1AAA-1" in urt_v, False, failures)

    # DK5EN-93: H={OE9ZZZ-5, OE1AAA-1, DK5EN-95}. OE1AAA-1 gedeckt durch
    # H(DK5EN-95); DK5EN-95 selbst liegt in own_heard; OE9ZZZ-5 bleibt
    # unabgedeckt -> exklusiv/MESH. Fund 2b: EDGE DK5EN-98->DK5EN-93 (eigener
    # Call als Hoerer) darf in h_n NICHT auftauchen.
    _check("verdict 93 h_n", urt_v["DK5EN-93"]["h_n"], ["DK5EN-95", "OE1AAA-1", "OE9ZZZ-5"], failures)
    _check("verdict 93 eigenes_urteil", urt_v["DK5EN-93"]["eigenes_urteil"], "exklusiv", failures)
    _check("verdict 93 exklusive_knoten", urt_v["DK5EN-93"]["exklusive_knoten"], ["OE9ZZZ-5"], failures)
    _check("verdict 93 firmware_meshneed", urt_v["DK5EN-93"]["firmware_meshneed"], "MESH", failures)
    _check("verdict 93 firmware_verdict", urt_v["DK5EN-93"]["firmware_verdict"], "EXCL", failures)
    _check("verdict 93 match", urt_v["DK5EN-93"]["uebereinstimmung"], True, failures)

    # Fund 3: DK5EN-94 und DK5EN-96 hoeren beide NUR OE2BBB-9 und sonst
    # niemand -- jeder einzeln redundant, aber verdict=EXCL (niemand ausser
    # mir hoert SIE selbst) waehrend meshneed=RED (was SIE hoeren, ist
    # gedeckt) -- genau die Divergenz aus Fund 1.
    _check("verdict 94 eigenes_urteil", urt_v["DK5EN-94"]["eigenes_urteil"], "redundant", failures)
    _check("verdict 94 firmware_verdict", urt_v["DK5EN-94"]["firmware_verdict"], "EXCL", failures)
    _check("verdict 94 match", urt_v["DK5EN-94"]["uebereinstimmung"], True, failures)
    _check("verdict 96 eigenes_urteil", urt_v["DK5EN-96"]["eigenes_urteil"], "redundant", failures)
    _check("verdict 96 firmware_verdict", urt_v["DK5EN-96"]["firmware_verdict"], "EXCL", failures)
    _check("verdict 96 match", urt_v["DK5EN-96"]["uebereinstimmung"], True, failures)

    # Fund 2a: DK5EN-99 hoert nur DK5EN-97, den ich selbst direkt (ME) hoere
    # -- das allein deckt ihn, unabhaengig von anderen Nachbarn.
    _check("verdict 99 h_n", urt_v["DK5EN-99"]["h_n"], ["DK5EN-97"], failures)
    _check("verdict 99 eigenes_urteil", urt_v["DK5EN-99"]["eigenes_urteil"], "redundant", failures)
    _check("verdict 99 match", urt_v["DK5EN-99"]["uebereinstimmung"], True, failures)

    # DK5EN-97: H ist leer -> per Definition redundant/RED. Die ROW-Zeile
    # traegt bewusst das falsche <meshneed>=MESH, um die Abweichungs-
    # Erkennung zu pruefen -- das MUSS als Abweichung auftauchen.
    _check("verdict 97 h_n", urt_v["DK5EN-97"]["h_n"], [], failures)
    _check("verdict 97 eigenes_urteil", urt_v["DK5EN-97"]["eigenes_urteil"], "redundant", failures)
    _check("verdict 97 firmware_meshneed", urt_v["DK5EN-97"]["firmware_meshneed"], "MESH", failures)
    _check("verdict 97 match", urt_v["DK5EN-97"]["uebereinstimmung"], False, failures)

    n_excl_v = [r for r in urt_v.values() if r["eigenes_urteil"] == "exklusiv"]
    n_red_v = [r for r in urt_v.values() if r["eigenes_urteil"] == "redundant"]
    _check("verdict anzahl exklusiv", len(n_excl_v), 1, failures)
    _check("verdict anzahl redundant", len(n_red_v), 5, failures)
    _check("verdict abweichungen anzahl", len(res_v["4_urteil"]["abweichungen"]), 1, failures)
    if res_v["4_urteil"]["abweichungen"]:
        _check(
            "verdict abweichung rufzeichen",
            res_v["4_urteil"]["abweichungen"][0]["rufzeichen"],
            "DK5EN-97",
            failures,
        )

    # 4b: minimale Deckungsmenge (Greedy, siehe Handrechnung im Fixture-Kopf)
    # und die eine wechselseitig redundante Gruppe (Fund 3).
    deck_v = res_v["4b_deckung"]
    _check("verdict universum_groesse", deck_v["universum_groesse"], 3, failures)
    _check("verdict deckungsmenge", deck_v["deckungsmenge"], ["DK5EN-93", "DK5EN-94"], failures)
    _check("verdict deckungsmenge_groesse", deck_v["deckungsmenge_groesse"], 2, failures)
    _check(
        "verdict wechselseitig redundante gruppen",
        deck_v["wechselseitig_redundante_gruppen"],
        [{"nachbarn": ["DK5EN-94", "DK5EN-96"], "gemeinsame_knoten": ["OE2BBB-9"]}],
        failures,
    )

    # -- 4) Stage-2-Erweiterung (docs/nbr-logformat.md): <snr> bei ME/EDGE
    #    (neues und altes Feldformat gemischt in einer Datei), [NBR]|SYM, und
    #    dass NEED/CANCEL?/CANCEL/REFUSE (mit ihrem neuen <inferred>-Feld)
    #    sauber als unbekannter Untertyp durchgereicht werden, statt als
    #    "foreign_line" oder als Parserfehler zu zaehlen --
    state_s2 = parse_files([TESTDATA_DIR / "nbr_sample_stage2.log"])
    res_s2 = analyze(state_s2)
    r_s2 = res_s2["1_rahmen"]
    _check("stage2 zeilen_gesamt", r_s2["zeilen_gesamt"], 25, failures)
    _check("stage2 nbr_zeilen", r_s2["nbr_zeilen"], 15, failures)
    _check("stage2 verworfen_gesamt", r_s2["verworfen_gesamt"], 10, failures)
    _check(
        "stage2 verworfen_gruende",
        r_s2["verworfen_gruende"],
        {
            "no_timestamp": 6,
            "unknown_subtype:CANCEL": 1,
            "unknown_subtype:CANCEL?": 1,
            "unknown_subtype:NEED": 1,
            "unknown_subtype:REFUSE": 1,
        },
        failures,
    )
    _check("stage2 reboots", r_s2["reboots"], 0, failures)

    # ME: gemischtes Format. DK5EN-93 hat drei neue Zeilen (snr -8, -6, NA),
    # DK5EN-94 eine alte Zeile ganz ohne <snr>-Feld -- beide muessen sauber
    # parsen, RSSI unveraendert, SNR bzw. None je nach Fall.
    _check("stage2 me count", len(state_s2.me), 4, failures)
    _check("stage2 me93[0] rssi", state_s2.me[0].rssi, -80, failures)
    _check("stage2 me93[0] snr", state_s2.me[0].snr, -8, failures)
    _check("stage2 me93[2] snr (NA)", state_s2.me[2].snr, None, failures)
    _check("stage2 me94 snr (altes Format)", state_s2.me[3].snr, None, failures)

    nb_s2 = {row["rufzeichen"]: row for row in res_s2["2_nachbarschaft"]["je_nachbar"]}
    _check("stage2 nb93 rssi_min", nb_s2["DK5EN-93"]["rssi_min"], -82, failures)
    _check("stage2 nb93 rssi_max", nb_s2["DK5EN-93"]["rssi_max"], -79, failures)
    _check("stage2 nb93 snr_median", nb_s2["DK5EN-93"]["snr_median"], -7.0, failures)
    _check("stage2 nb93 snr_min", nb_s2["DK5EN-93"]["snr_min"], -8, failures)
    _check("stage2 nb93 snr_max", nb_s2["DK5EN-93"]["snr_max"], -6, failures)
    _check("stage2 nb93 snr_ohne_bericht", nb_s2["DK5EN-93"]["snr_ohne_bericht"], 1, failures)
    _check("stage2 nb94 snr_median (altes Format)", nb_s2["DK5EN-94"]["snr_median"], None, failures)
    _check("stage2 nb94 snr_ohne_bericht", nb_s2["DK5EN-94"]["snr_ohne_bericht"], 1, failures)
    # RSSI-Statistik bleibt trotz des neuen SNR-Feldes unveraendert vorhanden.
    _check("stage2 nb94 rssi_min", nb_s2["DK5EN-94"]["rssi_min"], -90, failures)

    # EDGE: gemischtes Format. Neue Zeilen tragen rssi=0 (Zelle speichert keine
    # RSSI mehr) und ein <snr>; eine alte Zeile ohne <snr> hat weiterhin eine
    # echte (von Null verschiedene) RSSI -- beide muessen parsen.
    _check("stage2 edge count", len(state_s2.edges), 3, failures)
    _check("stage2 edge[0] rssi (neu, immer 0)", state_s2.edges[0].rssi, 0, failures)
    _check("stage2 edge[0] snr", state_s2.edges[0].snr, -9, failures)
    _check("stage2 edge[1] snr", state_s2.edges[1].snr, -11, failures)
    _check("stage2 edge[2] rssi (altes Format)", state_s2.edges[2].rssi, -77, failures)
    _check("stage2 edge[2] snr (altes Format)", state_s2.edges[2].snr, None, failures)

    # SYM: nur geloggte (ergebnisveraendernde) Annahmen, kein Zeilenfehler.
    sym_s2 = res_s2["10_symmetrie"]
    _check("stage2 sym anzahl_gesamt", sym_s2["anzahl_gesamt"], 4, failures)
    _check(
        "stage2 sym pro_rolle",
        sym_s2["pro_rolle"],
        {"ALT": 1, "COVER": 1, "HASF": 2},
        failures,
    )
    _check("stage2 sym je_paar anzahl", len(sym_s2["je_paar"]), 3, failures)
    if sym_s2["je_paar"]:
        top = sym_s2["je_paar"][0]
        _check("stage2 sym top paar x", top["x"], "DK5EN-95", failures)
        _check("stage2 sym top paar m", top["m"], "DK5EN-93", failures)
        _check("stage2 sym top paar anzahl", top["anzahl"], 2, failures)
        _check("stage2 sym top paar snr_median", top["snr_median"], -11.0, failures)

    # -- 5) Stufe-3-Erweiterung (docs/nbr-logformat.md, --nbrreport): RPT,
    #    RPTSUM, RPTTX, SYM-Rolle VETO, DROP-Grund RPT -- alle fuenf muessen
    #    sauber parsen (RPT mit fehlendem Feld als "malformed:RPT", nie als
    #    "foreign_line"), und Abschnitt 11 muss die richtigen Kennzahlen
    #    liefern. Die Handrechnung steht als Kommentarkopf im Fixture.
    state_s3 = parse_files([TESTDATA_DIR / "nbr_sample_stage3.log"])
    res_s3 = analyze(state_s3)
    r_s3 = res_s3["1_rahmen"]
    _check("stage3 zeilen_gesamt", r_s3["zeilen_gesamt"], 21, failures)
    _check("stage3 nbr_zeilen", r_s3["nbr_zeilen"], 14, failures)
    _check("stage3 verworfen_gesamt", r_s3["verworfen_gesamt"], 7, failures)
    _check(
        "stage3 verworfen_gruende",
        r_s3["verworfen_gruende"],
        {"no_timestamp": 6, "malformed:RPT": 1},
        failures,
    )
    _check("stage3 reboots", r_s3["reboots"], 0, failures)

    hn = res_s3["11_hn_berichte"]
    berichte = {r["rufzeichen"]: r for r in hn["berichte_je_sender"]}
    _check("stage3 berichte anzahl sender", len(berichte), 2, failures)
    _check("stage3 DL2JA-2 anzahl", berichte["DL2JA-2"]["anzahl"], 2, failures)
    _check("stage3 DL2JA-2 vollstaendig", berichte["DL2JA-2"]["vollstaendig"], 1, failures)
    _check("stage3 DL2JA-2 gekuerzt", berichte["DL2JA-2"]["gekuerzt"], 1, failures)
    _check("stage3 DL2JA-2 k_median", berichte["DL2JA-2"]["k_median"], 5.5, failures)
    _check("stage3 DL2JA-2 heard_median", berichte["DL2JA-2"]["heard_median"], 6.0, failures)
    _check("stage3 DL2JA-2 applied_gesamt", berichte["DL2JA-2"]["applied_gesamt"], 2, failures)
    _check("stage3 DB0ISM-1 anzahl", berichte["DB0ISM-1"]["anzahl"], 1, failures)
    _check("stage3 DB0ISM-1 vollstaendig", berichte["DB0ISM-1"]["vollstaendig"], 1, failures)

    status = {r["rufzeichen"]: r for r in hn["status_je_sender"]}
    _check("stage3 DL2JA-2 status ok", status["DL2JA-2"]["ok"], 2, failures)
    _check("stage3 DL2JA-2 status self", status["DL2JA-2"]["self"], 1, failures)
    _check("stage3 DL2JA-2 status norow", status["DL2JA-2"]["norow"], 1, failures)
    _check("stage3 DB0ISM-1 status ok", status["DB0ISM-1"]["ok"], 1, failures)
    _check("stage3 self_gesamt", hn["self_gesamt"], 1, failures)
    _check("stage3 norow_gesamt", hn["norow_gesamt"], 1, failures)

    kanten = {(k["x"], k["m"]): k for k in hn["kanten_angewendet"]}
    _check("stage3 kanten anzahl", len(kanten), 2, failures)
    _check("stage3 kante DL2JA-2/93 anzahl", kanten[("DL2JA-2", "DK5EN-93")]["anzahl"], 2, failures)
    _check("stage3 kante DL2JA-2/93 snr_median", kanten[("DL2JA-2", "DK5EN-93")]["snr_median"], 8.0, failures)
    _check("stage3 kante DB0ISM-1/93 anzahl", kanten[("DB0ISM-1", "DK5EN-93")]["anzahl"], 1, failures)
    _check("stage3 kante DB0ISM-1/93 snr_median", kanten[("DB0ISM-1", "DK5EN-93")]["snr_median"], 5.0, failures)

    eigene = hn["eigene_berichte"]
    _check("stage3 rpttx anzahl", eigene["anzahl"], 2, failures)
    _check("stage3 rpttx laenge_median", eigene["laenge_median"], 60.0, failures)
    _check("stage3 rpttx laenge_min", eigene["laenge_min"], 58, failures)
    _check("stage3 rpttx laenge_max", eigene["laenge_max"], 62, failures)

    veto = {(v["x"], v["m"]): v for v in hn["veto_je_paar"]}
    _check("stage3 veto_gesamt", hn["veto_gesamt"], 3, failures)
    _check("stage3 veto paare", len(veto), 2, failures)
    _check("stage3 veto DK5EN-95/93 anzahl", veto[("DK5EN-95", "DK5EN-93")]["anzahl"], 2, failures)
    _check("stage3 veto DK5EN-95/93 snr_median", veto[("DK5EN-95", "DK5EN-93")]["snr_median"], -8.0, failures)
    _check("stage3 veto DK5EN-96/94 anzahl", veto[("DK5EN-96", "DK5EN-94")]["anzahl"], 1, failures)

    _check("stage3 rpt_malformed_gesamt", hn["rpt_malformed_gesamt"], 1, failures)
    _check("stage3 drop pro_grund", res_s3["8_verworfene_frames"]["pro_grund"], {"RPT": 1}, failures)

    # VETO gehoert NICHT in Abschnitt 10 (angewendete Annahmen) -- diese
    # Fixture hat ausschliesslich VETO-Zeilen, Abschnitt 10 muss leer bleiben.
    _check("stage3 sym (VETO ausgeschlossen) anzahl_gesamt", res_s3["10_symmetrie"]["anzahl_gesamt"], 0, failures)

    # -- 6) --fetch --dry-run darf das Netz nie anfassen --
    import unittest.mock as mock

    with mock.patch(
        "subprocess.run",
        side_effect=AssertionError("--dry-run darf subprocess.run nie aufrufen"),
    ):
        out_dir = Path("/tmp/nbrlog-selftest-should-not-exist")
        try:
            do_fetch("martin@rpizero.local:~/meshlog/dk5en-98/", out_dir, dry_run=True)
        except AssertionError as exc:
            failures.append(str(exc))
        finally:
            if out_dir.exists() and not any(out_dir.iterdir()):
                out_dir.rmdir()

    rsync_cmd, _scp_cmd = build_fetch_commands(
        "martin@rpizero.local:~/meshlog/dk5en-98/", Path("/tmp/x")
    )
    _check("fetch rsync cmd", rsync_cmd[:2], ["rsync", "-az"], failures)
    _check(
        "fetch rsync source",
        rsync_cmd[2],
        "martin@rpizero.local:~/meshlog/dk5en-98/",
        failures,
    )

    if failures:
        print(f"SELBSTTEST FEHLGESCHLAGEN ({len(failures)} Abweichung(en)):")
        for f in failures:
            print(f"  - {f}")
        return 1

    print(
        "Selbsttest bestanden: 24h-Fixture, korrupte Fixture und Verdict-Beispiel "
        "liefern alle erwarteten Kennzahlen."
    )
    return 0


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Analysiert den [NBR]-Mitschnitt der Nachbarschaftsmatrix (24-h-Dauertest)."
    )
    ap.add_argument(
        "logs", nargs="*", type=Path,
        help="Logdateien (auch .gz), werden in Dateinamensreihenfolge gelesen",
    )
    ap.add_argument(
        "--fetch", metavar="[USER@]HOST:PFAD",
        help="Logs per rsync (Fallback scp) von HOST:PFAD holen, bevor ausgewertet wird",
    )
    ap.add_argument(
        "--dry-run", action="store_true",
        help="mit --fetch: nur den Abrufbefehl anzeigen, nichts abrufen und nichts auswerten",
    )
    ap.add_argument(
        "--workdir", type=Path, default=None,
        help="Zielverzeichnis fuer --fetch (Standard: ~/Downloads/nbrlog-<host>/)",
    )
    ap.add_argument("--since", metavar="YYYY-MM-DD", help="nur Zeilen ab diesem Datum auswerten")
    ap.add_argument("--out", type=Path, default=None, help="Markdown-Bericht in diese Datei schreiben")
    ap.add_argument("--json", dest="json_out", type=Path, default=None, help="Auswertung zusaetzlich als JSON schreiben")
    ap.add_argument("--self-test", action="store_true", help="gegen die eingebauten Fixtures pruefen, ohne Netz")
    args = ap.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if args.fetch and args.logs:
        raise SystemExit("--fetch und Logdateien als Argumente schliessen sich aus -- eins von beiden.")

    since_date: date | None = None
    if args.since:
        try:
            since_date = datetime.strptime(args.since, "%Y-%m-%d").date()
        except ValueError:
            raise SystemExit(f"--since erwartet YYYY-MM-DD, bekommen: {args.since!r}") from None

    if args.fetch:
        workdir = args.workdir or default_fetch_workdir(args.fetch)
        do_fetch(args.fetch, workdir, args.dry_run)
        if args.dry_run:
            return 0
        log_paths = collect_log_files(workdir)
        if not log_paths:
            raise SystemExit(f"keine .log/.gz-Dateien in {workdir} gefunden")
    else:
        log_paths = args.logs

    if not log_paths:
        raise SystemExit(
            "keine Logdateien angegeben (LOGDATEI..., --fetch oder --self-test erforderlich)"
        )

    state = parse_files(log_paths, since_date)
    res = analyze(state)
    md_text = render_md(res)

    if args.out:
        args.out.write_text(md_text, encoding="utf-8")
        maybe_prettify(args.out)
        print(f"geschrieben: {args.out}")
    else:
        print(md_text)

    if args.json_out:
        args.json_out.write_text(
            json.dumps(res, indent=2, ensure_ascii=False, default=str), encoding="utf-8"
        )
        print(f"geschrieben: {args.json_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
