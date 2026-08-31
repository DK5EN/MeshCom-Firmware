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
    [TFT];sleeping;<0|1>;bl;<n>;timer_age_ms;<n>
    [SCREEN];ms;<n>;crc;<hex0>,<hex1>,...,<hex7>;nonblack;<n>;total;76800;t_ms;<n>[;sleeping;1]
    [SCREEN];err;<reason>
    [BOOT];msg;<text>
    [BOOT];audio;file;<name>   [BOOT];audio;cw   [BOOT];audio;none
    [BOOT];init;sd;<0|1>;touch;<0|1>;kb;<0|1>;psram_buf;<0|1>;t_ms;<n>
    [DISPTEST];begin;phase;<s>;stride;<n>;w;<n>;h;<n>;steps;<n>;ms;<n>
    [DISPTEST];step;<phase>;n;<i>;crc;<hex8>;px;<n>;ms;<n>
    [DISPTEST];end;steps;<n>;ms;<n>
    [DISPTEST];err;<reason>[;<detail>]
    [INJ];raw;len;<bytes>;res;<n>
    [INJ];raw;err;<reason>
    [INJ];loratx;start;n;<n>;ms;<n>
    [INJ];loratx;q;<i>;id;<hex>
    [INJ];loratx;done;<queued>/<n>
    [SPITRACE];flush;<seq>;users;T<c>,S<c>,L<c>;user;<hex>;ctrl;<hex>;clock;<hex>;chg;<none|list>
    [TOUCH];inj;<tap|down|up>;x;<n>;y;<n>

[UISTAT] also accepts two optional trailing fields, tft_sleeping;<0|1> and
bl;<n> -- older firmware omits them, newer firmware appends them.

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
        bt: List[str] = []
        if i < len(parts) and parts[i] == "bt":
            raw_bt = _need("bt")
            bt = [a for a in raw_bt.split(",") if a and a != "-"]
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
        "bt": bt,
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


_UISTAT_REQUIRED = {
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
}
_UISTAT_OPTIONAL = {"tft_sleeping", "bl", "scroll_y", "scroll_bottom", "ml_y1", "ml_y2", "last_y1", "last_y2", "scr_h"}


def _parse_uistat(parts: List[str]) -> Optional[Dict[str, Any]]:
    d = _kv_ints(parts)
    if d is None:
        return None
    keys = set(d.keys())
    if not _UISTAT_REQUIRED <= keys:
        return None
    if not keys <= (_UISTAT_REQUIRED | _UISTAT_OPTIONAL):
        return None
    return {"kind": "UISTAT", **d}


def _parse_screen(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    if parts[0] == "err":
        reason = ";".join(parts[1:]) if len(parts) > 1 else ""
        return {"kind": "SCREEN", "variant": "err", "reason": reason}

    # variant "crc": ms,<n>,crc,<h0>,...,<h7>,nonblack,<n>,total,<n>,t_ms,<n>[,sleeping,1]
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
        crc_raw = _need("crc")
        crc = crc_raw.split(",")
        if len(crc) != 8 or not all(crc):
            return None
        nonblack = int(_need("nonblack"))
        total = int(_need("total"))
        t_ms = int(_need("t_ms"))
        sleeping: Optional[int] = None
        if i < len(parts) and parts[i] == "sleeping":
            sleeping = int(_need("sleeping"))
        if i != len(parts):
            return None
    except (ValueError, IndexError):
        return None

    return {
        "kind": "SCREEN",
        "variant": "crc",
        "ms": ms,
        "crc": crc,
        "nonblack": nonblack,
        "total": total,
        "t_ms": t_ms,
        "sleeping": sleeping,
    }


def _parse_boot(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    variant = parts[0]
    if variant == "msg":
        text = ";".join(parts[1:])
        return {"kind": "BOOT", "variant": "msg", "text": text}
    if variant == "audio":
        if len(parts) == 2 and parts[1] in ("cw", "none"):
            return {"kind": "BOOT", "variant": "audio", "what": parts[1], "file": None}
        if len(parts) == 3 and parts[1] == "file":
            return {"kind": "BOOT", "variant": "audio", "what": "file", "file": parts[2]}
        return None
    if variant == "init":
        d = _kv_ints(parts[1:])
        if d is None:
            return None
        required = {"sd", "touch", "kb", "psram_buf", "t_ms"}
        optional = {"touch_tries"}  # TM-33 (a): GT911 begin() attempts, firmware >= 2026-08-29
        keys = set(d.keys())
        if not required <= keys or not keys <= required | optional:
            return None
        return {"kind": "BOOT", "variant": "init", **d}
    return None


def _parse_disptest(parts: List[str]) -> Optional[Dict[str, Any]]:
    if not parts:
        return None
    variant = parts[0]
    if variant == "err":
        return {
            "kind": "DISPTEST",
            "variant": "err",
            "reason": parts[1] if len(parts) > 1 else "",
            "detail": ";".join(parts[2:]) if len(parts) > 2 else None,
        }
    if variant == "begin":
        if len(parts) != 13 or parts[1] != "phase":
            return None
        d = _kv_ints(parts[3:])
        if d is None or set(d.keys()) != {"stride", "w", "h", "steps", "ms"}:
            return None
        return {"kind": "DISPTEST", "variant": "begin", "phase": parts[2], **d}
    if variant == "end":
        d = _kv_ints(parts[1:])
        if d is None or set(d.keys()) != {"steps", "ms"}:
            return None
        return {"kind": "DISPTEST", "variant": "end", **d}
    if variant == "step":
        # step,<phase>,n,<i>,crc,<hex8>,px,<n>,ms,<n>
        if len(parts) != 10 or parts[2] != "n" or parts[4] != "crc":
            return None
        d = _kv_ints([parts[2], parts[3]] + parts[6:])
        if d is None or set(d.keys()) != {"n", "px", "ms"}:
            return None
        crc_raw = parts[5]
        if len(crc_raw) != 8:
            return None
        try:
            crc = int(crc_raw, 16)
        except ValueError:
            return None
        return {
            "kind": "DISPTEST",
            "variant": "step",
            "phase": parts[1],
            "crc": crc,
            **d,
        }
    return None


def _parse_inj(parts: List[str]) -> Optional[Dict[str, Any]]:
    """[INJ];raw;... (--injectraw) and [INJ];loratx;... (--loratx)."""
    if not parts:
        return None
    top = parts[0]
    if top == "raw":
        if len(parts) < 2:
            return None
        if parts[1] == "err":
            reason = ";".join(parts[2:]) if len(parts) > 2 else ""
            return {"kind": "INJ", "variant": "raw_err", "reason": reason}
        if parts[1] == "len":
            d = _kv_ints(parts[1:])
            if d is None or set(d.keys()) != {"len", "res"}:
                return None
            return {"kind": "INJ", "variant": "raw_len", "len": d["len"], "res": d["res"]}
        return None
    if top == "loratx":
        if len(parts) < 2:
            return None
        sub = parts[1]
        if sub == "start":
            d = _kv_ints(parts[2:])
            if d is None or set(d.keys()) != {"n", "ms"}:
                return None
            return {"kind": "INJ", "variant": "loratx_start", "n": d["n"], "ms": d["ms"]}
        if sub == "q":
            if len(parts) != 5 or parts[3] != "id":
                return None
            try:
                i = int(parts[2])
            except ValueError:
                return None
            return {"kind": "INJ", "variant": "loratx_q", "i": i, "id": parts[4]}
        if sub == "done":
            if len(parts) != 3:
                return None
            queued_s, _, n_s = parts[2].partition("/")
            if not n_s or not queued_s.isdigit() or not n_s.isdigit():
                return None
            return {
                "kind": "INJ",
                "variant": "loratx_done",
                "queued": int(queued_s),
                "n": int(n_s),
            }
        return None
    return None


def _parse_spitrace(parts: List[str]) -> Optional[Dict[str, Any]]:
    if len(parts) != 12 or parts[0] != "flush":
        return None
    try:
        seq = int(parts[1])
    except ValueError:
        return None
    if parts[2] != "users" or parts[4] != "user" or parts[6] != "ctrl" or parts[8] != "clock" or parts[10] != "chg":
        return None
    users: Dict[str, int] = {}
    for tok in parts[3].split(","):
        if len(tok) < 2:
            return None
        try:
            users[tok[0]] = int(tok[1:])
        except ValueError:
            return None
    if set(users.keys()) != {"T", "S", "L"}:
        return None
    chg_raw = parts[11]
    chg = [] if chg_raw == "none" else chg_raw.split(",")
    return {
        "kind": "SPITRACE",
        "variant": "flush",
        "seq": seq,
        "users": users,
        "user": parts[5],
        "ctrl": parts[7],
        "clock": parts[9],
        "chg": chg,
    }


def _parse_touch(parts: List[str]) -> Optional[Dict[str, Any]]:
    if len(parts) != 6 or parts[0] != "inj" or parts[2] != "x" or parts[4] != "y":
        return None
    what = parts[1]
    if what not in ("tap", "down", "up"):
        return None
    try:
        x = int(parts[3])
        y = int(parts[5])
    except ValueError:
        return None
    return {"kind": "TOUCH", "variant": "inj", "what": what, "x": x, "y": y}


_DISPATCH = {
    "DISPTEST": _parse_disptest,
    "REDRAW": _parse_redraw,
    "REFR": lambda p: _kv_check(p, "REFR", {"ms", "px", "t_ms"}, exact=True),
    "REFRSTART": lambda p: _kv_check(p, "REFRSTART", {"ms", "areas"}, exact=True),
    "UISTAT": _parse_uistat,
    "TFT": lambda p: _kv_check(
        p, "TFT", {"sleeping", "bl", "timer_age_ms"}, exact=True
    ),
    "SCREEN": _parse_screen,
    "BOOT": _parse_boot,
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
    "INJ": _parse_inj,
    "SPITRACE": _parse_spitrace,
    "TOUCH": _parse_touch,
}


def parse_line(line: str) -> Optional[Dict[str, Any]]:
    """Parse one raw serial line into a record dict, or None if unrecognized.

    Robust to leading/trailing whitespace and a trailing ';'. Never raises --
    any line that doesn't exactly fit a known shape (including a line that
    merely starts with '[' but isn't one of ours) yields None.
    """
    line = line.strip()
    # The firmware echoes the command without a newline, so a reply can arrive
    # glued to it ("--uistat[UISTAT];..."). Skip anything before the first '['.
    lb = line.find("[")
    if lb == -1:
        return None
    line = line[lb:]
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


# --------------------------------------------------------------------------
# TM-41 [DISPTEST]: host-side reference renderer
# --------------------------------------------------------------------------
#
# The T-Deck panel does not drive MISO, so --screencrc cannot say what is on
# the glass (docs/tdeck-findings-20260828.md). --disptest therefore CRC32s the
# exact byte block it hands to tft.pushColors() and prints one CRC per frame;
# the functions below re-render those frames here and produce the CRC the
# device must have printed. Both sides must rasterise identically, so the
# geometry is integer-only and defined once, here and in
# src/t-deck/tdeck_debug.cpp (dt_render()), in doubled pixel coordinates:
#
#     X = 2*x - (W-1),  Y = 2*y - (H-1)      centre is exactly (0, 0)
#
#     square   |X| <= 2h and |Y| <= 2h                 (h = 1..160 px)
#     circle   X*X + Y*Y <= (2r)*(2r)                  (r = 1..200 px)
#     triangle all three integer edge functions >= 0   (vertices on r = 100 px)
#
# A frame is 320x240 RGB565, row-major, high byte first -- LV_COLOR_16_SWAP=1
# plus pushColors(..., swap=False) put the memory byte order straight on the
# wire, so the CRC is over the bytes the panel actually received.

import zlib
from math import isqrt

DISPTEST_W = 320
DISPTEST_H = 240
DISPTEST_PX = DISPTEST_W * DISPTEST_H
DISPTEST_SQUARE_MAX = 160
DISPTEST_CIRCLE_MAX = 200
DISPTEST_TRI_STEPS = 24
DISPTEST_TRI_TURNS = 3
DISPTEST_TRI_FRAMES = DISPTEST_TRI_STEPS * DISPTEST_TRI_TURNS * 2
DISPTEST_TRI_R2 = 200

# round(1024 * sin(2*pi*i/24)); literal copy of DT_SIN24 in tdeck_debug.cpp
DISPTEST_SIN24 = (
    0, 265, 512, 724, 887, 989, 1024, 989,
    887, 724, 512, 265, 0, -265, -512, -724,
    -887, -989, -1024, -989, -887, -724, -512, -265,
)

DISPTEST_BARS = (0xFFFF, 0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F, 0x0000)
DISPTEST_FILLS = (0xF800, 0xFFE0, 0x07E0, 0x001F, 0xF81F)
DISPTEST_PHASES = ("invert", "colors", "square", "circle", "triangle")


def _dt_sin(i: int) -> int:
    return DISPTEST_SIN24[i % DISPTEST_TRI_STEPS]


def _dt_cos(i: int) -> int:
    return DISPTEST_SIN24[(i + 6) % DISPTEST_TRI_STEPS]


def _px_row(colour: int) -> bytes:
    return bytes(((colour >> 8) & 0xFF, colour & 0xFF)) * DISPTEST_W


def _ceil_div(p: int, q: int) -> int:
    """ceil(p/q) for q > 0, integers only."""
    return -((-p) // q)


def _x_range(x_lo: int, x_hi: int) -> Tuple[int, int]:
    """Doubled-coordinate span [x_lo, x_hi] -> inclusive pixel column range."""
    off = DISPTEST_W - 1
    return (
        max(0, _ceil_div(x_lo + off, 2)),
        min(DISPTEST_W - 1, (x_hi + off) // 2),
    )


def disptest_steps(phase: str, stride: int = 1) -> int:
    """Number of frames the firmware emits for one phase at this stride."""
    stride = max(1, int(stride))
    if phase == "invert":
        return 2
    if phase == "colors":
        return 10
    if phase == "square":
        return _ceil_div(DISPTEST_SQUARE_MAX, stride)
    if phase == "circle":
        return _ceil_div(DISPTEST_CIRCLE_MAX, stride)
    if phase == "triangle":
        return DISPTEST_TRI_FRAMES
    raise ValueError(f"unknown disptest phase: {phase!r}")


def disptest_triangle_vertices(i: int) -> List[Tuple[int, int]]:
    """The three doubled-coordinate vertices of triangle frame i, wound so that
    'inside' is 'every edge function >= 0' -- same order as dt_render()."""
    half = DISPTEST_TRI_FRAMES // 2
    a = (i if i < half else DISPTEST_TRI_FRAMES - 1 - i) % DISPTEST_TRI_STEPS
    v = []
    for k in range(3):
        ai = a + (DISPTEST_TRI_STEPS // 3) * k
        v.append(((DISPTEST_TRI_R2 * _dt_cos(ai)) >> 10,
                  (DISPTEST_TRI_R2 * _dt_sin(ai)) >> 10))
    area2 = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[1][1] - v[0][1]) * (v[2][0] - v[0][0])
    if area2 < 0:
        v[1], v[2] = v[2], v[1]
    return v


def _disptest_spans(phase: str, i: int, stride: int) -> Dict[int, Tuple[int, int]]:
    """Inclusive [x0, x1] pixel span of the foreground shape, per row.

    Span form of the per-pixel inside-tests in dt_render(): both describe the
    same pixel set (test_tdeck_parse.py checks that against a brute-force
    reference), the spans are just fast enough to run 500 frames in Python.
    """
    spans: Dict[int, Tuple[int, int]] = {}
    if phase == "square":
        h2 = 2 * min(DISPTEST_SQUARE_MAX, (i + 1) * stride)
        y0 = max(0, _ceil_div(DISPTEST_H - 1 - h2, 2))
        y1 = min(DISPTEST_H - 1, (DISPTEST_H - 1 + h2) // 2)
        x0, x1 = _x_range(-h2, h2)
        for y in range(y0, y1 + 1):
            spans[y] = (x0, x1)
        return spans
    if phase == "circle":
        r4 = (2 * min(DISPTEST_CIRCLE_MAX, (i + 1) * stride)) ** 2
        for y in range(DISPTEST_H):
            yy = 2 * y - (DISPTEST_H - 1)
            rem = r4 - yy * yy
            if rem < 0:
                continue
            d = isqrt(rem)
            x0, x1 = _x_range(-d, d)
            if x0 <= x1:
                spans[y] = (x0, x1)
        return spans
    if phase == "triangle":
        v = disptest_triangle_vertices(i)
        for y in range(DISPTEST_H):
            yy = 2 * y - (DISPTEST_H - 1)
            lo, hi = -10**9, 10**9
            empty = False
            for k in range(3):
                vx1, vy1 = v[k]
                vx2, vy2 = v[(k + 1) % 3]
                dx, dy = vx2 - vx1, vy2 - vy1
                # edge function: dx*(Y-vy1) - dy*(X-vx1) = a*X + c, a = -dy
                a = -dy
                c = dx * (yy - vy1) + dy * vx1
                if a > 0:
                    lo = max(lo, _ceil_div(-c, a))
                elif a < 0:
                    hi = min(hi, c // (-a))
                elif c < 0:
                    empty = True
                    break
            if empty or lo > hi:
                continue
            x0, x1 = _x_range(lo, hi)
            if x0 <= x1:
                spans[y] = (x0, x1)
        return spans
    return spans


def disptest_frame(phase: str, i: int, stride: int = 1) -> bytes:
    """The exact byte block --disptest hands to tft.pushColors() for one step."""
    stride = max(1, int(stride))
    if i < 0 or i >= disptest_steps(phase, stride):
        raise ValueError(f"step {i} out of range for phase {phase!r} at stride {stride}")

    if phase == "invert":
        band = DISPTEST_H // 8
        rows = []
        for y in range(DISPTEST_H):
            c = DISPTEST_BARS[y // band]
            rows.append(_px_row((~c) & 0xFFFF if i == 1 else c))
        return b"".join(rows)

    if phase == "colors":
        c = DISPTEST_FILLS[i % 5]
        if i >= 5:
            c = (~c) & 0xFFFF
        return _px_row(c) * DISPTEST_H

    bg, fg = (0xFFFF, 0x0000) if phase == "square" else (0x0000, 0xFFFF)
    frame = bytearray(_px_row(bg) * DISPTEST_H)
    fg_row = _px_row(fg)
    for y, (x0, x1) in _disptest_spans(phase, i, stride).items():
        off = y * DISPTEST_W * 2
        frame[off + 2 * x0 : off + 2 * (x1 + 1)] = fg_row[: 2 * (x1 - x0 + 1)]
    return bytes(frame)


def disptest_crc(phase: str, i: int, stride: int = 1) -> int:
    """CRC32 (zlib/IEEE, the polynomial crc32_update() uses) of one frame."""
    return zlib.crc32(disptest_frame(phase, i, stride))


def disptest_expected(
    phase: str = "full", stride: int = 1
) -> List[Tuple[str, int, int]]:
    """(phase, step index, expected CRC32) for every frame of a --disptest run."""
    stride = max(1, int(stride))
    phases = DISPTEST_PHASES if phase in ("full", "", None) else (phase,)
    out: List[Tuple[str, int, int]] = []
    for p in phases:
        for i in range(disptest_steps(p, stride)):
            out.append((p, i, disptest_crc(p, i, stride)))
    return out
