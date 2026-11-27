#!/usr/bin/env python3
"""Regenerate test/test_settings_migration/fixtures/ (FORMAL-02 wave 1).

For every release tag since the keyed settings store landed (2026-10-01,
commit 6966c527) this writes two files:

  <tag>.rec   the keyed record (key=value lines) a node running that tag
              would have written: that tag's schema rows (config_json.h
              CFG_FIELD_LIST + settings_schema.h SETTINGS_PERSIST_ONLY_LIST),
              one distinct, valid, non-default-looking value per key, encoded
              the way settings_store.cpp encodes it (integers in decimal,
              FLOAT as %.9g of the float32, DOUBLE as %.17g, strings escaped).
  INDEX       the tags, one per line, oldest first (the test reads it).
  <tag>.keys  one line per schema row: key, type, platform, lo, hi, esc
              (platform = common | esp32 | tdeck | nrf52; lo/hi "-" = no
              range). The ESP32 NVS path has no shared codec, so the test
              checks per-key type stability against this manifest.

Schemas are read with read-only `git show <tag>:<path>`; nothing is checked
out. Values are deterministic (hash of the key) and chosen to differ from the
CURRENT struct defaults, so a load is observable. A fixture, once written, is
frozen: a later default change must not rewrite what an old release wrote. A
normal run only adds the files of new tags and rewrites INDEX.

  python3 tools/settings_fixtures.py            # add fixtures of new tags
  python3 tools/settings_fixtures.py --check    # exit 1 if INDEX or a file is missing
  python3 tools/settings_fixtures.py --rebuild  # rewrite every fixture (only after reading the diff)
"""
from __future__ import annotations

import argparse
import hashlib
import re
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "test" / "test_settings_migration" / "fixtures"
SINCE = "2026-10-01"
TAG_GLOB = "v4.40a.*"
TAG_RE = re.compile(r"^v4\.40a\.\d\d\.\d\d$")  # release tags only, every month
SCHEMA_FILES = ("src/config_json.h", "src/settings_schema.h")

# Macro-valued range bounds that a text scan cannot evaluate.
MACRO_VALUES = {
    "MAXHOP_TEXT_MIN": 1.0,
    "MAXHOP_TEXT_MAX": 6.0,
    "TX_POWER_MIN": -9.0,
    "TX_POWER_MAX": 22.0,
}
# Rows whose value must be a specific member / sentinel-free choice.
FORCED: dict[str, str] = {
    "node_dht": "22",        # allowed set {11, 22}, 11 is the default
    "node_power": "7",       # escape row (-20 = not set); pick a real value
    "max_hop_text": "3",     # escape row (0 = not set)
    # plausible ESP32 radio values: the sanitize pass resets implausible ones
    "node_freq": "%.9g" % struct.unpack("<f", struct.pack("<f", 433.175))[0],
    "node_bw": "125",
    "node_sf": "9",
    "node_cr": "6",
    "node_ctry": "3",
}
STR_CAP_FALLBACK = 2         # content bytes when the struct size is unknown
STR_CAP_MAX = 6


@dataclass
class Row:
    key: str
    ctype: str          # CFG_STR CFG_CHR CFG_INT CFG_U32 CFG_BOOL CFG_FLT CFG_DBL
    member: str
    lo: Optional[float]
    hi: Optional[float]
    esc: bool
    platform: str


def git(*args: str) -> Optional[str]:
    r = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def release_tags() -> list[str]:
    out = git("tag", "-l", TAG_GLOB) or ""
    dated: list[tuple[str, str]] = []
    for t in out.split():
        if not TAG_RE.match(t):
            continue
        date = (git("log", "-1", "--format=%cs", t) or "").strip()
        if date >= SINCE:
            dated.append((date, t))
    return [t for _, t in sorted(dated)]  # by commit date: 01.xx of the next year sorts after 12.xx


def split_args(s: str) -> list[str]:
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    parts.append(cur.strip())
    return parts


def num(tok: str) -> Optional[float]:
    t = tok.replace("(double)", "").strip()
    try:
        return float(t)
    except ValueError:
        return MACRO_VALUES.get(t)


def platform_of(stack: list[tuple[str, bool]]) -> str:
    plat = "common"
    for cond, in_else in stack:
        if "BOARD_T_DECK" in cond and not in_else:
            return "tdeck"
        if "ESP32" in cond:
            plat = "nrf52" if in_else else "esp32"
    return plat


ROW_RE = re.compile(r'^\s*X\(\s*"([^"]+)"\s*,(.*)\)\s*\\?\s*$')


def parse_rows(text: str) -> list[Row]:
    rows: list[Row] = []
    stack: list[tuple[str, bool]] = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#if"):
            stack.append((s, False))
        elif s.startswith("#else") and stack:
            stack[-1] = (stack[-1][0], True)
        elif s.startswith("#elif") and stack:
            stack[-1] = (s, False)
        elif s.startswith("#endif") and stack:
            stack.pop()
        m = ROW_RE.match(line)
        if not m:
            continue
        args = split_args(m.group(2))
        ctype, member = args[0], args[1]
        rest = args[2:]
        lo = hi = None
        if rest and rest[0] != "CFG_NORANGE":
            lo, hi = num(rest[0]), num(rest[1])
            rest = rest[2:]
        else:
            rest = rest[1:]
        esc = any(a.startswith("CFG_ESC") for a in rest) or "CFG_ESC" in m.group(2)
        if lo is not None and hi is not None and lo > hi:
            lo = hi = None
        rows.append(Row(m.group(1), ctype, member, lo, hi, esc, platform_of(stack)))
    return rows


def schema_at(tag: str) -> list[Row]:
    rows: list[Row] = []
    for p in SCHEMA_FILES:
        text = git("show", f"{tag}:{p}")
        if text is None:
            raise SystemExit(f"{tag}: {p} missing (schema predates the keyed store?)")
        rows += parse_rows(text)
    seen: set[str] = set()
    for r in rows:
        if r.key in seen:
            raise SystemExit(f"{tag}: duplicate key {r.key}")
        seen.add(r.key)
    return rows


def string_caps(tag: str) -> dict[str, int]:
    """member -> char[N] size at the tag (best effort, ESP32 + shared struct)."""
    caps: dict[str, int] = {}
    for p in ("src/meshcom_settings.h", "src/esp32/esp32_flash.h", "src/nrf52/WisBlock-API.h"):
        text = git("show", f"{tag}:{p}") or ""
        for m in re.finditer(r"A\(\s*char\s*,\s*(\w+)\s*,\s*\[(\d+)\]\s*(\[(\d+)\])?", text):
            caps[m.group(1)] = int(m.group(4) or m.group(2))
        for m in re.finditer(r"\bchar\s+(\w+)\s*\[(\d+)\]\s*(\[(\d+)\])?", text):
            caps.setdefault(m.group(1), int(m.group(4) or m.group(2)))
    return caps


def struct_defaults() -> dict[str, float]:
    """member -> numeric default of the CURRENT struct (working tree, ESP32 value), so fixtures never equal it."""
    out: dict[str, float] = {}
    for line in (ROOT / "src" / "meshcom_settings.h").read_text().splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).rstrip().rstrip("\\").strip()
        m = re.match(r"M\((.*)\)$", line)
        if not m:
            continue
        a = split_args(m.group(1))
        if len(a) < 3:
            continue
        d = ",".join(a[2:]).strip()
        if d.startswith("MC_PLATFORM_DEFAULT("):
            d = split_args(d[len("MC_PLATFORM_DEFAULT("):-1])[0]
        d = {"true": "1", "false": "0"}.get(d, d)
        try:
            out[a[1]] = float(int(d, 0)) if re.match(r"^-?(0x[0-9a-fA-F]+|\d+)$", d) else float(d)
        except ValueError:
            pass
    return out


DEFAULTS = struct_defaults()


def h(key: str) -> int:
    return int.from_bytes(hashlib.sha256(key.encode()).digest()[:4], "big")


def f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", v))[0]


def esc_str(s: str) -> str:
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r")


def value_for(r: Row, caps: dict[str, int]) -> str:
    if r.key in FORCED:
        return FORCED[r.key]
    k = h(r.key)
    frac = 0.30 + (k % 400) / 1000.0           # 0.30 .. 0.70, never at an edge
    base = r.member.split("[")[0]
    if r.ctype == "CFG_STR":
        cap = min(max(caps.get(base, 0) - 1, 0), STR_CAP_MAX) or STR_CAP_FALLBACK
        alphabet = "abcdefghijklmnopqrstuvwxyz0123456789"
        body = "".join(alphabet[(k >> (3 * i)) % len(alphabet)] for i in range(cap))
        # one escapable byte in some values so the escape path is part of every era
        if cap >= 3 and k % 5 == 0:
            body = body[0] + "\\" + body[2:]
            return esc_str(body)
        return body
    if r.ctype == "CFG_CHR":
        return str(ord("A") + k % 26)
    dflt = DEFAULTS.get(base)
    if r.ctype == "CFG_BOOL":
        return "0" if dflt == 1.0 else "1"
    if r.ctype in ("CFG_INT", "CFG_U32"):
        if r.lo is not None and r.hi is not None:
            v = int(r.lo + (r.hi - r.lo) * frac)
            v = max(int(r.lo), min(int(r.hi), v))
            for cand in (v, int(r.hi), int(r.lo), v + 1, v - 1):
                if int(r.lo) <= cand <= int(r.hi) and cand != dflt:
                    return str(cand)
            return str(v)
        v = 100 + k % 900
        return str(v + 1 if v == dflt else v)
    if r.ctype == "CFG_FLT":
        lo, hi = (r.lo, r.hi) if r.lo is not None else (1.0, 9.0)
        v = f32(round(lo + (hi - lo) * frac, 3))
        return "%.9g" % v
    if r.ctype == "CFG_DBL":
        lo, hi = (r.lo, r.hi) if r.lo is not None else (1.0, 9.0)
        return "%.17g" % (lo + (hi - lo) * frac)
    raise SystemExit(f"unknown type {r.ctype} for {r.key}")


def type_name(c: str) -> str:
    return c.removeprefix("CFG_")


def build(tag: str) -> tuple[str, str]:
    rows = schema_at(tag)
    caps = string_caps(tag)
    rec = "".join(f"{r.key}={value_for(r, caps)}\n" for r in rows)

    def b(x: Optional[float]) -> str:
        return "-" if x is None else repr(x)

    keys = "".join(
        f"{r.key} {type_name(r.ctype)} {r.platform} {b(r.lo)} {b(r.hi)} {int(r.esc)}\n" for r in rows
    )
    return rec, keys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="do not write; exit 1 if INDEX differs or a file is missing")
    ap.add_argument("--rebuild", action="store_true", help="rewrite existing fixtures too")
    args = ap.parse_args()
    tags = release_tags()
    if len(tags) < 20:
        print(f"only {len(tags)} tags found since {SINCE}; is the checkout shallow?", file=sys.stderr)
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    stale = 0
    index = "".join(t + "\n" for t in tags)
    ip = OUT / "INDEX"
    if args.check:
        if not ip.exists() or ip.read_text() != index:
            print(f"differs: {ip.relative_to(ROOT)}")
            stale += 1
    else:
        ip.write_text(index)
    for t in tags:
        paths = [OUT / f"{t}.rec", OUT / f"{t}.keys"]
        if all(p.exists() for p in paths) and not args.rebuild:
            continue  # frozen
        if args.check:
            print(f"missing: {t}")
            stale += 1
            continue
        rec, keys = build(t)
        for p, body in zip(paths, (rec, keys)):
            p.write_text(body)
        print(f"{t}: {rec.count(chr(10))} keys")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
