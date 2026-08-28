#!/usr/bin/env python3
"""Parsers for the T-Deck Plus GUI-instrumentation serial line formats.

Pure, importable, unit-testable: no serial I/O here (see tdeck_harness.py for
that). Every parser is deliberately conservative -- a line that does not match
the exact expected shape for its bracket tag returns None from parse_line()
rather than raising, so a stray/garbled/unknown line from the device never
crashes a long-running capture.

Line formats (see tools/bench/tdeck_harness.py docstring / task brief for the
authoritative list):

    [REDRAW];ms;<n>;obj;0x<hex>;cls;<name>;area;<x1>;<y1>;<x2>;<y2>;ra;0x<hex>[;name;<tv|msg_list|map_ta>]
    [REDRAW];dropped;<n>
    [REFR];ms;<n>;px;<n>;t_ms;<n>
    [REFRSTART];ms;<n>;areas;<n>
    [UISTAT];tab;<n>;drawer;<0|1>;objs;<n>;msg_list;<n>;inv_total;<n>;refr_total;<n>;last_refr_px;<n>;last_refr_ms;<n>;redrawlog;<0|1>;heap_free;<n>;heap_min;<n>;psram_free;<n>
    [TAB];<idx>;<name>
    [TAB];active;<idx>
    [TAB];set;<idx>;inv_delta;<n>;
    [TAB];err;range
    [DRAWER];<0|1>
    [INJECT];ok;id;<hex>;dst;<s>;src;<s>;len;<n>
    [INJECT];err;<reason>
    [AUDIO];play;<what>[;vol;<n>]
    [AUDIO];err;<kind>;<detail>
    [AUDIO];info;<text>
    [AUDIO];eof;<file>
    [INSTR-HEAP];<tag>;int_free;<n>;int_min;<n>;int_largest;<n>;psram_free;<n>;psram_largest;<n>
    [INSTR-FLUSH];n;<n>;total_us;<n>;avg_us;<n>;max_us;<n>
    [INSTR-LOOP];n;<n>;total_us;<n>;avg_us;<n>;max_us;<n>
    [INSTR-GUI];msg_list_children;<n>;active_tab_bubbles;<n>;persisted_msgs;<n>;map_points;<i>

Every parsed record is a dict with a "kind" key (the bracket tag, without
brackets, e.g. "REDRAW", "UISTAT", "INSTR-HEAP"). Records whose device line
carries more than one shape under the same tag (REDRAW, TAB, INJECT, AUDIO)
additionally carry a "variant" key. Hex fields (obj/ra/id addresses) are kept
as the original "0x..." string -- that is what addr2line wants, and callers
that need an int can convert it themselves.
"""

from __future__ import annotations

from collections import Counter
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


def _split_fields(rest: str) -> List[str]:
    """Split the part of a line after the closing bracket into fields.

    Drops a leading separating ';' and any number of trailing empty fields
    (a firmware line may end with a stray trailing ';').
    """
    if rest.startswith(";"):
        rest = rest[1:]
    parts = rest.split(";") if rest else []
    while parts and parts[-1] == "":
        parts.pop()
    return parts


def _kv_ints(parts: Sequence[str]) -> Optional[Dict[str, int]]:
    """Parse an even-length [key, value, key, value, ...] list into an all-int dict.

    Returns None if the list has odd length, is empty, or any value fails to
    parse as an int.
    """
    if not parts or len(parts) % 2 != 0:
        return None
    out: Dict[str, int] = {}
    for i in range(0, len(parts), 2):
        key = parts[i]
        try:
            out[key] = int(parts[i + 1])
        except ValueError:
            return None
    return out


def _kv_check(
    parts: Sequence[str], kind: str, required_keys: Iterable[str], exact: bool = False
) -> Optional[Dict[str, Any]]:
    d = _kv_ints(parts)
    if d is None:
        return None
    required = set(required_keys)
    if exact:
        if set(d.keys()) != required:
            return None
    elif not required <= set(d.keys()):
        return None
    return {"kind": kind, **d}


def _parse_redraw(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    if parts[0] == "dropped":
        if len(parts) != 2:
            return None
        try:
            n = int(parts[1])
        except ValueError:
            return None
        return {"kind": "REDRAW", "variant": "dropped", "dropped": n}

    # variant "obj": ms,<n>,obj,0x..,cls,<name>,area,<x1>,<y1>,<x2>,<y2>,ra,0x..[,name,<val>]
    i = 0

    def _need(tag: str) -> str:
        nonlocal i
        if i >= len(parts) or parts[i] != tag:
            raise ValueError(f"expected {tag!r}")
        val = parts[i + 1]
        i += 2
        return val

    try:
        ms = int(_need("ms"))
        obj = _need("obj")
        cls = _need("cls")
        if i >= len(parts) or parts[i] != "area":
            return None
        x1, y1, x2, y2 = (int(v) for v in parts[i + 1 : i + 5])
        i += 5
        ra = _need("ra")
        name: Optional[str] = None
        if i < len(parts):
            name = _need("name")
        if i != len(parts):
            return None
    except (ValueError, IndexError):
        return None

    return {
        "kind": "REDRAW",
        "variant": "obj",
        "ms": ms,
        "obj": obj,
        "cls": cls,
        "area": (x1, y1, x2, y2),
        "ra": ra,
        "name": name,
    }


def _parse_tab(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    if parts[0] == "active":
        if len(parts) != 2:
            return None
        try:
            return {"kind": "TAB", "variant": "active", "idx": int(parts[1])}
        except ValueError:
            return None
    if parts[0] == "set":
        if len(parts) != 4 or parts[2] != "inv_delta":
            return None
        try:
            return {
                "kind": "TAB",
                "variant": "set",
                "idx": int(parts[1]),
                "inv_delta": int(parts[3]),
            }
        except ValueError:
            return None
    if parts[0] == "err":
        reason = parts[1] if len(parts) > 1 else ""
        return {"kind": "TAB", "variant": "err", "reason": reason}
    # variant "line": [TAB];<idx>;<name>
    if len(parts) == 2:
        try:
            idx = int(parts[0])
        except ValueError:
            return None
        return {"kind": "TAB", "variant": "line", "idx": idx, "name": parts[1]}
    return None


def _parse_drawer(parts: List[str]) -> Optional[Dict[str, Any]]:
    if len(parts) != 1:
        return None
    try:
        return {"kind": "DRAWER", "state": int(parts[0])}
    except ValueError:
        return None


def _parse_inject(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    if parts[0] == "ok":
        if len(parts) != 9:
            return None
        rest = parts[1:]
        keys = rest[0::2]
        vals = rest[1::2]
        if keys != ["id", "dst", "src", "len"]:
            return None
        try:
            length = int(vals[3])
        except ValueError:
            return None
        return {
            "kind": "INJECT",
            "variant": "ok",
            "id": vals[0],
            "dst": vals[1],
            "src": vals[2],
            "len": length,
        }
    if parts[0] == "err":
        reason = ";".join(parts[1:]) if len(parts) > 1 else ""
        return {"kind": "INJECT", "variant": "err", "reason": reason}
    return None


def _parse_audio(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    variant = parts[0]
    if variant == "play":
        if len(parts) == 2:
            return {"kind": "AUDIO", "variant": "play", "what": parts[1], "vol": None}
        if len(parts) == 4 and parts[2] == "vol":
            try:
                vol = int(parts[3])
            except ValueError:
                return None
            return {"kind": "AUDIO", "variant": "play", "what": parts[1], "vol": vol}
        return None
    if variant == "err":
        if len(parts) < 3:
            return None
        return {
            "kind": "AUDIO",
            "variant": "err",
            "err_kind": parts[1],
            "detail": ";".join(parts[2:]),
        }
    if variant == "info":
        if len(parts) < 2:
            return None
        return {"kind": "AUDIO", "variant": "info", "text": ";".join(parts[1:])}
    if variant == "eof":
        if len(parts) < 2:
            return None
        return {"kind": "AUDIO", "variant": "eof", "file": ";".join(parts[1:])}
    return None


def _parse_instr_heap(parts: List[str]) -> Optional[Dict[str, Any]]:
    if len(parts) < 3:
        return None
    tag = parts[0]
    d = _kv_ints(parts[1:])
    if d is None:
        return None
    required = {"int_free", "int_min", "int_largest", "psram_free", "psram_largest"}
    if set(d.keys()) != required:
        return None
    return {"kind": "INSTR-HEAP", "tag": tag, **d}


_DISPATCH = {
    "REDRAW": _parse_redraw,
    "REFR": lambda p: _kv_check(p, "REFR", {"ms", "px", "t_ms"}, exact=True),
    "REFRSTART": lambda p: _kv_check(p, "REFRSTART", {"ms", "areas"}, exact=True),
    "UISTAT": lambda p: _kv_check(
        p,
        "UISTAT",
        {
            "tab",
            "drawer",
            "objs",
            "msg_list",
            "inv_total",
            "refr_total",
            "last_refr_px",
            "last_refr_ms",
            "redrawlog",
            "heap_free",
            "heap_min",
            "psram_free",
        },
        exact=True,
    ),
    "TAB": _parse_tab,
    "DRAWER": _parse_drawer,
    "INJECT": _parse_inject,
    "AUDIO": _parse_audio,
    "INSTR-HEAP": _parse_instr_heap,
    "INSTR-FLUSH": lambda p: _kv_check(
        p, "INSTR-FLUSH", {"n", "total_us", "avg_us", "max_us"}, exact=True
    ),
    "INSTR-LOOP": lambda p: _kv_check(
        p, "INSTR-LOOP", {"n", "total_us", "avg_us", "max_us"}, exact=True
    ),
    "INSTR-GUI": lambda p: _kv_check(
        p,
        "INSTR-GUI",
        {"msg_list_children", "active_tab_bubbles", "persisted_msgs", "map_points"},
        exact=True,
    ),
}


def parse_line(line: str) -> Optional[Dict[str, Any]]:
    """Parse one raw serial line into a record dict, or None if unrecognized.

    Robust to leading/trailing whitespace and a trailing ';'. Never raises --
    any line that doesn't exactly fit a known shape (including a line that
    merely starts with '[' but isn't one of ours) yields None.
    """
    line = line.strip()
    if not line.startswith("["):
        return None
    end = line.find("]")
    if end == -1:
        return None
    tag = line[1:end]
    parser = _DISPATCH.get(tag)
    if parser is None:
        return None
    parts = _split_fields(line[end + 1 :])
    return parser(parts)


def redraw_summary(records: Iterable[Optional[Dict[str, Any]]]) -> Dict[str, Any]:
    """Aggregate REDRAW records: per (cls, name) counts, per-ra counts, totals.

    Non-REDRAW and None records are ignored. `dropped` sums the counts
    reported by any REDRAW;dropped records seen in the window.
    """
    total = 0
    dropped = 0
    by_cls_name: Counter[Tuple[Any, Any]] = Counter()
    by_ra: Counter[Any] = Counter()
    for r in records:
        if not r or r.get("kind") != "REDRAW":
            continue
        if r.get("variant") == "dropped":
            dropped += r.get("dropped", 0)
        elif r.get("variant") == "obj":
            total += 1
            by_cls_name[(r.get("cls"), r.get("name"))] += 1
            by_ra[r.get("ra")] += 1
    return {
        "total": total,
        "dropped": dropped,
        "by_cls_name": dict(by_cls_name),
        "by_ra": dict(by_ra),
    }


def refr_summary(
    records: Iterable[Optional[Dict[str, Any]]], window_seconds: Optional[float] = None
) -> Dict[str, Any]:
    """Aggregate REFR records: count, total px, mean/max t_ms.

    If window_seconds is given (and > 0), also reports refreshes_per_second
    and px_per_second over that window.
    """
    refrs = [r for r in records if r and r.get("kind") == "REFR"]
    count = len(refrs)
    sum_px = sum(r["px"] for r in refrs)
    mean_t_ms = (sum(r["t_ms"] for r in refrs) / count) if count else 0.0
    max_t_ms = max((r["t_ms"] for r in refrs), default=0)
    out: Dict[str, Any] = {
        "count": count,
        "sum_px": sum_px,
        "mean_t_ms": mean_t_ms,
        "max_t_ms": max_t_ms,
    }
    if window_seconds:
        out["refreshes_per_second"] = count / window_seconds
        out["px_per_second"] = sum_px / window_seconds
    return out


# Heap-ish keys shared between [UISTAT] records (heap_free/heap_min/psram_free)
# and [INSTR-HEAP] records (int_free/int_min/int_largest/psram_free/psram_largest).
_HEAP_KEYS = (
    "heap_free",
    "heap_min",
    "psram_free",
    "int_free",
    "int_min",
    "int_largest",
    "psram_largest",
)


def heap_delta(
    first: Optional[Dict[str, Any]], last: Optional[Dict[str, Any]]
) -> Dict[str, int]:
    """Delta (last - first) for every heap-ish key present in both records.

    Works for a pair of [UISTAT] records or a pair of [INSTR-HEAP] records
    (the two use different key names for the same idea; whichever keys are
    present in both inputs are diffed).
    """
    if not first or not last:
        return {}
    return {k: last[k] - first[k] for k in _HEAP_KEYS if k in first and k in last}
