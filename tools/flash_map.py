#!/usr/bin/env python3
"""Flash memory map of an ESP32 application image, from a GNU ld map file.

Attributes every byte that is stored in the flash image (code in .flash.text,
read-only data in .flash.rodata, IRAM code and DRAM data init values that are
loaded from flash, RTC and PHY IRAM sections) to

  * a library archive (libbt.a, libmbedtls.a, an Arduino library, the project's
    own src/ objects, ...) and the object file inside it,
  * a coarse group (ESP-IDF/system, WiFi, BLE (NimBLE), TLS/crypto, LwIP,
    Arduino core, C/C++ runtime, one group per third-party library, MeshCom src),
  * a symbol (taken from the -ffunction-sections / -fdata-sections input section
    name; merged string literals (.rodata.str*) are attributed to their object).

Sections that occupy RAM only (.dram0.bss, .noinit, .rtc_noinit, *_noload) are
not part of the image and are ignored.

Usage:
  flash_map.py MAP [--elf ELF] [--label NAME] [--json OUT] [--top N]
  flash_map.py diff OLD.json NEW.json [--top N]
  flash_map.py html A.json [B.json ...] -o OUT.html     (data-driven report)

Python 3 standard library only. With --elf the tool also runs the toolchain's
nm and objdump (found next to the PlatformIO toolchain) to add exact ELF symbol
sizes; without them the map alone is used.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import subprocess
import sys
import zlib
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable, Iterator

SCHEMA_VERSION = 1

# Output sections whose contents are stored in the flash image.
IMAGE_SECTIONS = {
    ".flash.text",
    ".flash.rodata",
    ".flash.appdesc",
    ".flash.init_array",
    ".iram0.text",
    ".iram0.vectors",
    ".dram0.data",
    ".rtc.text",
    ".rtc.data",
    ".rtc.force_fast",
    ".rtc.force_slow",
    ".rtc.entry.literal",
}
IMAGE_SECTION_PREFIXES = (".phyiram", ".phydram")

# Bundled Arduino-ESP32 libraries (framework-arduinoespressif32/libraries).
ARDUINO_LIBS = {
    "ArduinoOTA", "AsyncUDP", "BLE", "BluetoothSerial", "DNSServer", "EEPROM", "ESP32",
    "ESPmDNS", "ESP_I2S", "ESP_NOW", "Ethernet", "FFat", "FS", "HTTPClient", "HTTPUpdate",
    "HTTPUpdateServer", "LittleFS", "NetBIOS", "NetworkClientSecure", "Preferences", "SD",
    "SD_MMC", "SPI", "SPIFFS", "SimpleBLE", "Ticker", "USB", "Update", "WebServer", "WiFi",
    "WiFiClientSecure", "WiFiProv", "Wire",
}

G_SYSTEM = "ESP-IDF/system"
G_WIFI = "WiFi"
G_BLE = "BLE (NimBLE)"
G_TLS = "TLS/crypto"
G_LWIP = "LwIP"
G_ARDUINO = "Arduino core"
G_RUNTIME = "C/C++ runtime"
G_SRC = "MeshCom src"
G_PAD = "(padding / unattributed)"
G_OTHER = "Other"

SDK_GROUPS: dict[str, str] = {}
for _n in ("net80211", "pp", "phy", "wpa_supplicant", "coexist", "mesh", "esp_wifi", "esp_phy",
           "rtc", "espnow", "smartconfig", "wps", "wapi", "wpa", "wpa2", "wifi_provisioning",
           "WiFi", "WiFiProv"):
    SDK_GROUPS["lib" + _n] = G_WIFI
for _n in ("bt", "btdm_app", "nimble", "NimBLE-Arduino", "BLE", "esp_hid"):
    SDK_GROUPS["lib" + _n] = G_BLE
for _n in ("mbedtls", "mbedcrypto", "mbedx509", "mbedtls_2", "esp-tls", "esp_https_ota",
           "esp_https_server", "WiFiClientSecure", "NetworkClientSecure", "wolfssl"):
    SDK_GROUPS["lib" + _n] = G_TLS
for _n in ("lwip", "esp_netif", "tcpip_adapter", "esp_eth", "dhcpd"):
    SDK_GROUPS["lib" + _n] = G_LWIP
for _n in ("newlib", "c", "m", "gcc", "stdc++", "supc++", "nosys", "c_nano", "gcov"):
    SDK_GROUPS["lib" + _n] = G_RUNTIME
SDK_GROUPS["libFrameworkArduino"] = G_ARDUINO

HEX = r"0x[0-9a-fA-F]+"
RE_OUT_FULL = re.compile(rf"^(\S+)\s+({HEX})\s+({HEX})(?:\s+.*)?$")
RE_OUT_NAME = re.compile(r"^(\.\S+|COMMON)\s*$")
RE_ENTRY_FULL = re.compile(rf"^ (\S+)\s+({HEX})\s+({HEX})\s+(.+?)\s*$")
RE_ENTRY_NAME = re.compile(r"^ (\S+)\s*$")
RE_CONT = re.compile(rf"^\s+({HEX})\s+({HEX})\s+(.+?)\s*$")
RE_FILL = re.compile(rf"^ \*fill\*\s+({HEX})\s+({HEX})")
RE_ARCHIVE = re.compile(r"^(.*?)([^/\\]+\.a)\((.+)\)$")
RE_SDK_PATH = re.compile(r"tools/sdk/(esp32\w*)/")
RE_SRC = re.compile(r"(?:^|/)\.pio/build/[^/]+/src/(.+?)(?:\.o)?$")
RE_OBJ_DIR = re.compile(r"(?:^|/)\.pio/build/[^/]+/([^/]+)/(.+)$")


# ----------------------------------------------------------------- map parsing
def parse_map(lines: Iterable[str]) -> tuple[list[dict[str, Any]], dict[str, dict[str, int]]]:
    """Return (entries, output sections). Entries are input sections of every
    output section (filtered later); output sections carry addr and size."""
    entries: list[dict[str, Any]] = []
    outs: dict[str, dict[str, int]] = {}
    in_map = False
    out_name: str | None = None
    pending_out: str | None = None
    pending_in: str | None = None
    for raw in lines:
        line = raw.rstrip("\n")
        if not in_map:
            if line.startswith("Linker script and memory map"):
                in_map = True
            continue
        if not line.strip():
            continue
        if pending_out is not None:
            m = re.match(rf"^\s+({HEX})\s+({HEX})", line)
            if m:
                outs[pending_out] = {"addr": int(m.group(1), 16), "size": int(m.group(2), 16)}
                out_name = pending_out
                pending_out = None
                continue
            pending_out = None
        if pending_in is not None:
            m = RE_CONT.match(line)
            if m and out_name is not None:
                entries.append({"out": out_name, "name": pending_in,
                                "addr": int(m.group(1), 16), "size": int(m.group(2), 16),
                                "path": m.group(3)})
                pending_in = None
                continue
            pending_in = None
        c = line[0]
        if c != " ":
            m = RE_OUT_FULL.match(line)
            if m and (m.group(1).startswith(".") or m.group(1) == "COMMON"):
                out_name = m.group(1)
                outs[out_name] = {"addr": int(m.group(2), 16), "size": int(m.group(3), 16)}
                continue
            m = RE_OUT_NAME.match(line)
            if m:
                pending_out = m.group(1)
                continue
            out_name = None  # LOAD, OUTPUT, /DISCARD/ ...
            continue
        if out_name is None:
            continue
        m = RE_FILL.match(line)
        if m:
            entries.append({"out": out_name, "name": "*fill*", "addr": int(m.group(1), 16),
                            "size": int(m.group(2), 16), "path": ""})
            continue
        if line.startswith(" *") or line.startswith(" ["):
            continue
        m = RE_ENTRY_FULL.match(line)
        if m and not line.startswith("  "):
            entries.append({"out": out_name, "name": m.group(1), "addr": int(m.group(2), 16),
                            "size": int(m.group(3), 16), "path": m.group(4)})
            continue
        m = RE_ENTRY_NAME.match(line)
        if m and not line.startswith("  "):
            pending_in = m.group(1)
    return entries, outs


def parse_inclusions(lines: Iterable[str]) -> dict[str, tuple[str, str]]:
    """'Archive member included to satisfy reference' block: member path ->
    (referring object path, symbol). Lets a report say who pulled a library in."""
    res: dict[str, tuple[str, str]] = {}
    started = False
    member: str | None = None
    for raw in lines:
        line = raw.rstrip("\n")
        if not started:
            started = line.startswith("Archive member included")
            continue
        if line.startswith(("Discarded input", "Allocating common", "Memory Configuration",
                            "Linker script")):
            break
        if not line.strip():
            continue
        if not line.startswith(" "):
            member = line.strip()
            m = re.match(r"^(.*?\))\s{2,}(\S.*)$", member)  # referrer on the same line
            if m:
                member, line = m.group(1), " " + m.group(2)
            else:
                continue
        if member is not None:
            m = re.match(r"^\s*(.*?)\s+\((.+)\)\s*$", line)
            if m:
                res[member] = (m.group(1), m.group(2))
                member = None
    return res


def chain_root(path: str, inc: dict[str, tuple[str, str]], limit: int = 16) -> list[str]:
    """Follow referrers from a member up to a project source object (or a dead end)."""
    chain = [path]
    cur = path
    for _ in range(limit):
        ref = inc.get(cur)
        if not ref:
            break
        cur = ref[0]
        chain.append(cur)
        if RE_SRC.search(cur) and not RE_ARCHIVE.match(cur):
            break
    return chain


def sec_label(name: str) -> str:
    """Report label of an output section: the ~25 numbered .phyiram.N collapse into one."""
    return ".phyiram.*" if name.startswith(".phyiram") else (
        ".phydram.*" if name.startswith(".phydram") else name)


def is_image_section(name: str, extra: set[str] | None = None) -> bool:
    if extra is not None:
        return name in extra
    return name in IMAGE_SECTIONS or name.startswith(IMAGE_SECTION_PREFIXES)


# -------------------------------------------------------------- classification
def split_path(path: str) -> tuple[str, str]:
    """Return (container, object). Container is an archive basename, a project
    path (src/foo.cpp) or the object itself."""
    m = RE_ARCHIVE.match(path)
    if m:
        return m.group(2), m.group(3)
    m = RE_SRC.search(path)
    if m:
        return "src/" + m.group(1), "src/" + m.group(1)
    base = path.rsplit("/", 1)[-1]
    return base, base


def lib_name(archive: str) -> str:
    name = archive[:-2] if archive.endswith(".a") else archive
    return name[3:] if name.startswith("lib") else name


def classify(path: str) -> tuple[str, str, str]:
    """Return (group, container, object) for an input-section path."""
    if not path:
        return G_PAD, "(fill)", "(fill)"
    container, obj = split_path(path)
    if RE_SRC.search(path) and not RE_ARCHIVE.match(path):
        return G_SRC, container, obj
    if not container.endswith(".a"):
        return G_OTHER, container, obj
    stem = container[:-2]
    if stem in SDK_GROUPS:
        return SDK_GROUPS[stem], container, obj
    if "/tools/sdk/" in path:
        return G_SYSTEM, container, obj
    if "toolchain-" in path or "/gcc/" in path:
        return G_RUNTIME, container, obj
    name = lib_name(container)
    if name in ARDUINO_LIBS:
        return f"Arduino lib: {name}", container, obj
    if RE_OBJ_DIR.search(path):
        return f"lib: {name}", container, obj
    return G_OTHER, container, obj


_SYM_PREFIX = re.compile(
    r"^\.(?:literal|text|rodata|data|iram\d|dram\d|srodata|sdata|flash\.\w+|rtc\.\w+|phyiram\.?\d*"
    r"|phydram\.?\d*|init_array|ctors|dtors|eh_frame|gcc_except_table|xt_except_table)\.(.+)$"
)


STR_TAG = " [strings]"
_STR_SUFFIX = re.compile(r"^(.+)\.str\d+\.\d+$")


def symbol_of(sec: str, obj: str) -> str:
    """Symbol name from an input section name (function/data sections)."""
    m = _SYM_PREFIX.match(sec)
    if not m:
        return f"<{sec}> {obj}"
    rest = m.group(1)
    if rest.isdigit():  # .iram1.7, .phyiram.3: numbered anonymous sections
        return f"<{sec}> {obj}"
    if rest.startswith(("str1.", "str2.", "str4.", "str8.", "str")) and sec.startswith(".rodata"):
        return f"<string literals> {obj}"
    if rest.startswith("cst"):
        return f"<constants> {obj}"
    m2 = _STR_SUFFIX.match(rest)
    if m2 and sec.startswith(".rodata"):  # merged strings of one function: foo.str1.1
        return m2.group(1) + STR_TAG
    return rest


# ------------------------------------------------------------------ toolchain
def find_tool(suffix: str, chip: str) -> str | None:
    prefix = "xtensa-esp32s3-elf-" if chip == "esp32s3" else "xtensa-esp32-elf-"
    p = shutil.which(prefix + suffix)
    if p:
        return p
    pkg = "toolchain-xtensa-esp32s3" if chip == "esp32s3" else "toolchain-xtensa-esp32"
    cand = Path.home() / ".platformio" / "packages" / pkg / "bin" / (prefix + suffix)
    return str(cand) if cand.exists() else None


def demangle(names: list[str], cxxfilt: str | None) -> dict[str, str]:
    """Batch-demangle through c++filt; names that are not mangled map to themselves."""
    mangled = sorted({n.removesuffix(STR_TAG) for n in names if n.startswith("_Z")})
    if not mangled or not cxxfilt:
        return {}
    try:
        out = subprocess.run([cxxfilt], input="\n".join(mangled) + "\n", capture_output=True,
                             text=True, check=True, timeout=120).stdout.splitlines()
    except (OSError, subprocess.SubprocessError):
        return {}
    return dict(zip(mangled, out)) if len(out) == len(mangled) else {}


def elf_symbols(elf: Path, chip: str, ranges: list[tuple[int, int, str]],
                top: int) -> list[dict[str, Any]]:
    """Largest defined ELF symbols inside the image address ranges (nm -S -C)."""
    nm = find_tool("nm", chip)
    if not nm:
        return []
    try:
        out = subprocess.run([nm, "-S", "-C", "--defined-only", str(elf)], capture_output=True,
                             text=True, check=True, timeout=300).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    res: list[dict[str, Any]] = []
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4:
            continue
        try:
            addr, size = int(parts[0], 16), int(parts[1], 16)
        except ValueError:
            continue
        for lo, hi, sec in ranges:
            if lo <= addr < hi and size > 0:
                res.append({"name": parts[3], "type": parts[2], "bytes": size, "section": sec,
                            "addr": addr})
                break
    res.sort(key=lambda r: -r["bytes"])
    return res[:top]


# --------------------------------------------------------------------- report
def detect_chip(map_text_head: str) -> str:
    m = RE_SDK_PATH.search(map_text_head)
    return m.group(1) if m else "esp32"


def build_report(map_path: Path, *, label: str | None = None, top: int = 200,
                 elf: Path | None = None, sections: set[str] | None = None,
                 use_tools: bool = True) -> dict[str, Any]:
    with map_path.open(encoding="utf-8", errors="replace") as fh:
        text_head_lines: list[str] = []
        lines: list[str] = []
        for i, ln in enumerate(fh):
            lines.append(ln)
            if i < 4000:
                text_head_lines.append(ln)
    chip = detect_chip("".join(text_head_lines))
    entries, outs = parse_map(lines)
    inclusions = parse_inclusions(lines)
    del lines

    sec_bytes: dict[str, int] = defaultdict(int)
    groups: dict[str, dict[str, Any]] = {}
    archives: dict[str, dict[str, Any]] = {}
    objects: dict[tuple[str, str], dict[str, Any]] = {}
    symbols: dict[tuple[str, str], dict[str, Any]] = {}
    attributed: dict[str, int] = defaultdict(int)

    def bump(d: dict[str, Any], out: str, size: int) -> None:
        out = sec_label(out)
        d["bytes"] += size
        d["sections"][out] = d["sections"].get(out, 0) + size

    # The linker merges identical string literals (SEC_MERGE): later duplicates are listed
    # with their full size but point into bytes that were already counted. Only the part
    # beyond the highest address seen so far in the output section is new.
    last_end: dict[str, int] = {}
    for e in entries:
        out, size = e["out"], e["size"]
        if size <= 0 or not is_image_section(out, sections):
            continue
        end = e["addr"] + size
        size = end - max(e["addr"], last_end.get(out, 0)) if end > last_end.get(out, 0) else 0
        last_end[out] = max(end, last_end.get(out, 0))
        if size <= 0:
            continue
        group, container, obj = classify(e["path"])
        attributed[out] += size
        sec_bytes[sec_label(out)] += size
        g = groups.setdefault(group, {"bytes": 0, "sections": {}})
        bump(g, out, size)
        a = archives.setdefault(container, {"archive": container, "group": group, "bytes": 0,
                                            "sections": {}})
        bump(a, out, size)
        o = objects.setdefault((container, obj), {"archive": container, "object": obj,
                                                  "group": group, "bytes": 0, "sections": {},
                                                  "path": e["path"]})
        bump(o, out, size)
        if e["name"] != "*fill*":
            sym = symbol_of(e["name"], obj)
            s = symbols.setdefault((sym, obj), {"name": sym, "object": obj, "archive": container,
                                                "group": group, "bytes": 0, "sections": {}})
            bump(s, out, size)
    # Output-section bytes the input entries do not account for (ALIGN gaps).
    for out, info in outs.items():
        if is_image_section(out, sections) and info["size"] > attributed.get(out, 0):
            gap = info["size"] - attributed.get(out, 0)
            g = groups.setdefault(G_PAD, {"bytes": 0, "sections": {}})
            bump(g, out, gap)
            sec_bytes[sec_label(out)] += gap

    image_bytes = sum(sec_bytes.values())
    cxxfilt = find_tool("c++filt", chip) or shutil.which("c++filt") if use_tools else None
    sym_list = sorted(symbols.values(), key=lambda r: -r["bytes"])[:top]
    dm = demangle([s["name"] for s in sym_list], cxxfilt)
    for s in sym_list:
        base = s["name"].removesuffix(STR_TAG)
        s["demangled"] = dm.get(base, base) + (STR_TAG if base != s["name"] else "")

    for o in objects.values():
        ref = inclusions.get(o["path"])
        if ref:
            chain = chain_root(o["path"], inc=inclusions)
            o["pulled_by"] = f"{split_path(ref[0])[1]} ({ref[1]})"
            o["chain"] = [split_path(c)[1] for c in chain if c]
    src_files = sorted((o for o in objects.values() if o["group"] == G_SRC),
                       key=lambda r: -r["bytes"])
    g_total = {k: v for k, v in groups.items()}
    src_total = groups.get(G_SRC, {}).get("bytes", 0)

    report: dict[str, Any] = {
        "schema": SCHEMA_VERSION,
        "label": label or map_path.parent.name,
        "map": str(map_path),
        "chip": chip,
        "image_bytes": image_bytes,
        "src_bytes": src_total,
        "sections": dict(sorted(sec_bytes.items(), key=lambda kv: -kv[1])),
        "groups": dict(sorted(g_total.items(), key=lambda kv: -kv[1]["bytes"])),
        "archives": sorted(archives.values(), key=lambda r: -r["bytes"]),
        "objects": sorted(objects.values(), key=lambda r: -r["bytes"])[: max(top, 400)],
        "src_files": src_files,
        "symbols": sym_list,
    }
    if elf is not None and elf.exists() and use_tools:
        ranges = [(o["addr"], o["addr"] + o["size"], n) for n, o in outs.items()
                  if is_image_section(n, sections) and o["size"] > 0]
        syms = elf_symbols(elf, chip, ranges, top)
        report["elf_symbols"] = syms
    bin_path = map_path.with_name("firmware.bin")
    if bin_path.exists():
        data = bin_path.read_bytes()
        report["bin_bytes"] = len(data)
        report["bin_zlib9_bytes"] = len(zlib.compress(data, 9))
    return report


# ------------------------------------------------------------------ text/diff
def kb(n: int) -> str:
    return f"{n / 1024:8.1f} KB"


def print_text(rep: dict[str, Any], top: int, out=sys.stdout) -> None:
    img = rep["image_bytes"]
    out.write(f"{rep['label']} ({rep['chip']}): image {img} B ({kb(img).strip()})")
    if "bin_bytes" in rep:
        out.write(f", firmware.bin {rep['bin_bytes']} B")
    out.write("\n\nSections:\n")
    for n, b in rep["sections"].items():
        out.write(f"  {n:<22} {b:>9} {b * 100 / img:5.1f}%\n")
    out.write("\nGroups:\n")
    for n, g in rep["groups"].items():
        out.write(f"  {n:<32} {g['bytes']:>9} {g['bytes'] * 100 / img:5.1f}%\n")
    out.write(f"\nTop {top} archives:\n")
    for a in rep["archives"][:top]:
        out.write(f"  {a['archive']:<40} {a['bytes']:>9} {a['bytes'] * 100 / img:5.1f}%  {a['group']}\n")
    out.write(f"\nTop {top} symbols:\n")
    for s in rep["symbols"][:top]:
        out.write(f"  {s['bytes']:>8}  {s['demangled'][:70]:<70} {s['object']}\n")


def diff_reports(old: dict[str, Any], new: dict[str, Any], top: int = 30) -> list[str]:
    out = [f"image: {old['image_bytes']} -> {new['image_bytes']} "
           f"({new['image_bytes'] - old['image_bytes']:+d} B)"]

    def table(title: str, a: dict[str, int], b: dict[str, int]) -> None:
        rows = [(k, a.get(k, 0), b.get(k, 0)) for k in set(a) | set(b)]
        rows = [r for r in rows if r[1] != r[2]]
        rows.sort(key=lambda r: -abs(r[2] - r[1]))
        out.append(f"\n{title}:")
        for k, x, y in rows[:top]:
            out.append(f"  {y - x:+9d}  {k}  ({x} -> {y})")

    table("groups", {k: v["bytes"] for k, v in old["groups"].items()},
          {k: v["bytes"] for k, v in new["groups"].items()})
    table("archives", {a["archive"]: a["bytes"] for a in old["archives"]},
          {a["archive"]: a["bytes"] for a in new["archives"]})
    table("objects", {f"{o['archive']}:{o['object']}": o["bytes"] for o in old["objects"]},
          {f"{o['archive']}:{o['object']}": o["bytes"] for o in new["objects"]})
    return out


# ------------------------------------------------------------------ HTML/SVG
PALETTE = ["--c1", "--c2", "--c3", "--c4", "--c5", "--c6", "--c7", "--c8"]


def squarify(items: list[tuple[str, float]], x: float, y: float, w: float,
             h: float) -> list[tuple[str, float, float, float, float]]:
    """Squarified treemap layout; items sorted descending by value."""
    total = sum(v for _, v in items)
    if total <= 0 or w <= 0 or h <= 0:
        return []
    scale = w * h / total
    rest = [(n, v * scale) for n, v in items if v > 0]
    rects: list[tuple[str, float, float, float, float]] = []

    def worst(row: list[float], side: float) -> float:
        s = sum(row)
        return max(max(side * side * r / (s * s), s * s / (side * side * r)) for r in row)

    while rest:
        side = min(w, h)
        row = [rest[0]]
        i = 1
        while i < len(rest) and worst([a for _, a in row + [rest[i]]], side) <= worst(
                [a for _, a in row], side):
            row.append(rest[i])
            i += 1
        rest = rest[i:]
        s = sum(a for _, a in row)
        if w >= h:  # lay the row as a column on the left
            cw = s / h
            cy = y
            for n, a in row:
                rh = a / cw
                rects.append((n, x, cy, cw, rh))
                cy += rh
            x += cw
            w -= cw
        else:
            rh = s / w
            cx = x
            for n, a in row:
                rw = a / rh
                rects.append((n, cx, y, rw, rh))
                cx += rw
            y += rh
            h -= rh
    return rects


def fmt_b(n: int) -> str:
    return f"{n:,}"


def esc(s: str) -> str:
    return html.escape(s, quote=True)


# Fixed colour slot per named group (colour follows the entity, not its rank); every
# third-party library, Arduino lib and padding shares the neutral slot 8 and is told apart
# by its label.
GROUP_SLOT = {G_SRC: 0, G_WIFI: 1, G_SYSTEM: 2, G_BLE: 3, G_RUNTIME: 4, G_LWIP: 5, G_TLS: 6}
DARK_INK_SLOTS = {2, 3, 4, 7}  # aqua, yellow, magenta, neutral carry dark label text


def group_colors(names: list[str]) -> dict[str, str]:
    return {n: PALETTE[GROUP_SLOT.get(n, 7)] for n in names}


def slot_of(name: str) -> int:
    return GROUP_SLOT.get(name, 7)


def treemap_svg(rep: dict[str, Any], width: int = 960, height: int = 330) -> str:
    items = sorted(((n, g["bytes"]) for n, g in rep["groups"].items()), key=lambda kv: -kv[1])
    colors = group_colors([n for n, _ in items])
    img = rep["image_bytes"]
    parts = [f'<svg class="treemap" viewBox="0 0 {width} {height}" role="img" '
             f'aria-label="Treemap of the {esc(rep["label"])} image by group">']
    for n, x, y, w, h in squarify(items, 0, 0, width, height):
        b = dict(items)[n]
        parts.append(
            f'<g><title>{esc(n)}: {fmt_b(b)} B ({b * 100 / img:.1f}%)</title>'
            f'<rect x="{x + 1:.1f}" y="{y + 1:.1f}" width="{max(w - 2, 0):.1f}" '
            f'height="{max(h - 2, 0):.1f}" rx="3" style="fill:var({colors[n]})"/>')
        if w > 70 and h > 34:
            dk = " dk" if slot_of(n) in DARK_INK_SLOTS else ""
            parts.append(f'<text x="{x + 8:.1f}" y="{y + 20:.1f}" class="tl{dk}">'
                         f'{esc(n[:int(w / 7)])}</text>'
                         f'<text x="{x + 8:.1f}" y="{y + 36:.1f}" class="ts{dk}">{b / 1024:,.0f} KB '
                         f'· {b * 100 / img:.1f}%</text>')
        parts.append("</g>")
    parts.append("</svg>")
    return "".join(parts)


def stacked_bar(rep: dict[str, Any]) -> str:
    items = sorted(((n, g["bytes"]) for n, g in rep["groups"].items()), key=lambda kv: -kv[1])
    colors = group_colors([n for n, _ in items])
    img = rep["image_bytes"]
    segs = "".join(
        f'<span class="seg" style="flex:{b};background:var({colors[n]})" '
        f'title="{esc(n)}: {fmt_b(b)} B ({b * 100 / img:.1f}%)"></span>' for n, b in items)
    legend = "".join(
        f'<li><i style="background:var({colors[n]})"></i>{esc(n)} <b>{b / 1024:,.0f} KB</b></li>'
        for n, b in items[:12])
    return f'<div class="stack">{segs}</div><ul class="legend">{legend}</ul>'


def table_html(headers: list[str], rows: list[list[str]], num_cols: set[int]) -> str:
    th = "".join(f'<th class="{"n" if i in num_cols else ""}">{esc(h)}</th>'
                 for i, h in enumerate(headers))
    body = "".join(
        "<tr>" + "".join(f'<td class="{"n" if i in num_cols else ""}">{c}</td>'
                         for i, c in enumerate(r)) + "</tr>" for r in rows)
    return f'<div class="tw"><table><thead><tr>{th}</tr></thead><tbody>{body}</tbody></table></div>'


def board_tables(rep: dict[str, Any]) -> str:
    img = rep["image_bytes"]

    def pct(b: int) -> str:
        return f"{b * 100 / img:.1f}%"

    arch = [[f"<code>{esc(a['archive'])}</code>", esc(a["group"]), fmt_b(a["bytes"]), pct(a["bytes"])]
            for a in rep["archives"][:30]]
    obj = [[f"<code>{esc(o['object'])}</code>", f"<code>{esc(o['archive'])}</code>",
            fmt_b(o["bytes"]), pct(o["bytes"]),
            f"<code>{esc(' < '.join(o.get('chain', [])[1:4]))}</code>"] for o in rep["objects"][:40]]
    sym = [[f"<code>{esc(s['demangled'][:110])}</code>", f"<code>{esc(s['object'])}</code>",
            fmt_b(s["bytes"]), pct(s["bytes"])] for s in rep["symbols"][:40]]
    src = [[f"<code>{esc(o['object'])}</code>", fmt_b(o["bytes"]), pct(o["bytes"]),
            fmt_b(o["sections"].get(".flash.text", 0) + o["sections"].get(".iram0.text", 0)),
            fmt_b(sum(v for k, v in o["sections"].items() if "rodata" in k or "data" in k))]
           for o in rep["src_files"][:40]]
    grp = [[esc(n), fmt_b(g["bytes"]), pct(g["bytes"])] for n, g in rep["groups"].items()]
    secs = [[f"<code>{esc(n)}</code>", fmt_b(b), pct(b)] for n, b in rep["sections"].items()]
    return (
        "<h4>Groups</h4>" + table_html(["Group", "Bytes", "%"], grp, {1, 2})
        + "<h4>Image sections</h4>" + table_html(["Section", "Bytes", "%"], secs, {1, 2})
        + "<details open><summary>Top 30 archives</summary>"
        + table_html(["Archive", "Group", "Bytes", "%"], arch, {2, 3}) + "</details>"
        + "<details><summary>Top 40 objects</summary>"
        + table_html(["Object", "Archive", "Bytes", "%", "Pulled in by (first reference)"], obj,
                      {2, 3}) + "</details>"
        + "<details><summary>Top 40 symbols</summary>"
        + table_html(["Symbol", "Object", "Bytes", "%"], sym, {2, 3}) + "</details>"
        + f"<details open><summary>MeshCom src/ files ranked ({fmt_b(rep['src_bytes'])} B total)</summary>"
        + table_html(["File", "Bytes", "%", "Code B", "Data B"], src, {1, 2, 3, 4}) + "</details>")


CSS = """
:root{color-scheme:light;--bg:#fcfcfb;--panel:#ffffff;--ink:#0b0b0b;--ink2:#52514e;--line:#e3e2dd;
--accent:#2a78d6;--warn:#b25b00;--bad:#c0312f;--good:#12704f;
--c1:#2a78d6;--c2:#eb6834;--c3:#1baf7a;--c4:#eda100;--c5:#e87ba4;--c6:#008300;--c7:#4a3aa7;--c8:#8a8982}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){color-scheme:dark;--bg:#141413;
--panel:#1c1c1a;--ink:#f4f4f1;--ink2:#c3c2b7;--line:#33322f;--accent:#3987e5;--warn:#e0953a;--bad:#ee7a79;
--good:#43c297;--c1:#3987e5;--c2:#d95926;--c3:#199e70;--c4:#c98500;--c5:#d55181;--c6:#2fa02f;--c7:#9085e9;--c8:#9a998f}}
:root[data-theme="dark"]{color-scheme:dark;--bg:#141413;--panel:#1c1c1a;--ink:#f4f4f1;--ink2:#c3c2b7;
--line:#33322f;--accent:#3987e5;--warn:#e0953a;--bad:#ee7a79;--good:#43c297;--c1:#3987e5;--c2:#d95926;
--c3:#199e70;--c4:#c98500;--c5:#d55181;--c6:#2fa02f;--c7:#9085e9;--c8:#9a998f}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.55 -apple-system,BlinkMacSystemFont,
"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
main{max-width:1040px;margin:0 auto;padding:24px 16px 80px}
h1{font-size:26px;margin:0 0 4px}h2{font-size:21px;margin:40px 0 8px;padding-top:12px;border-top:1px solid var(--line)}
h3{font-size:17px;margin:28px 0 6px}h4{font-size:14px;margin:18px 0 6px;color:var(--ink2);
text-transform:uppercase;letter-spacing:.04em}
p,li{max-width:80ch}.sub{color:var(--ink2);margin:0 0 16px}
code{font:12.5px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;word-break:break-all}
.card{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin:12px 0}
.bluf{border-left:4px solid var(--accent)}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin:12px 0}
.kpi{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px 14px}
.kpi b{display:block;font-size:22px}.kpi span{color:var(--ink2);font-size:13px}
.tw{overflow-x:auto;margin:6px 0 10px}table{border-collapse:collapse;width:100%;font-size:13.5px}
th,td{padding:5px 10px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}
th{font-size:12px;color:var(--ink2);text-transform:uppercase;letter-spacing:.03em;white-space:nowrap}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
tbody tr:hover{background:color-mix(in srgb,var(--accent) 8%,transparent)}
.treemap{width:100%;height:auto;display:block}.treemap text{fill:#fff;font:600 13px -apple-system,sans-serif}
.treemap .ts{font-weight:400;font-size:12px}.treemap .dk{fill:#10100f}
.stack{display:flex;height:26px;border-radius:6px;overflow:hidden;gap:2px;margin:10px 0 6px}
.seg{min-width:2px}.legend{list-style:none;padding:0;margin:0;display:flex;flex-wrap:wrap;gap:4px 16px;
font-size:13px}.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px}
details{margin:10px 0}summary{cursor:pointer;font-weight:600;padding:4px 0}
.tag{display:inline-block;font-size:11px;font-weight:700;border-radius:999px;padding:1px 8px;
border:1px solid currentColor}.measured{color:var(--good)}.estimated{color:var(--warn)}
.bar{height:14px;background:var(--line);border-radius:7px;position:relative;margin:4px 0 2px}
.bar i{position:absolute;left:0;top:0;bottom:0;border-radius:7px;background:var(--accent)}
.bar u{position:absolute;top:-3px;bottom:-3px;width:2px;background:var(--bad)}
@media (max-width:600px){h1{font-size:22px}th,td{padding:4px 6px}}
"""


def build_html(reports: list[dict[str, Any]], *, title: str = "Flash memory map",
               intro_html: str = "", mid_html: str = "", extra_html: str = "") -> str:
    boards = ""
    for rep in reports:
        boards += (f'<section><h2 id="b-{esc(rep["label"])}">{esc(rep["label"])} '
                   f'<small>({esc(rep["chip"])}, {fmt_b(rep["image_bytes"])} B image)</small></h2>'
                   f'<div class="card">{treemap_svg(rep)}{stacked_bar(rep)}</div>'
                   f'{board_tables(rep)}</section>')
    return (f'<!doctype html><html lang="en"><head><meta charset="utf-8">'
            f'<meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{esc(title)}</title><style>{CSS}</style></head><body><main>'
            f'<h1>{esc(title)}</h1>{intro_html}{mid_html}{boards}{extra_html}</main></body></html>')


# ------------------------------------------------------------------------ CLI
def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "diff":
        ap = argparse.ArgumentParser(prog="flash_map.py diff", description="Diff two JSON reports.")
        ap.add_argument("old")
        ap.add_argument("new")
        ap.add_argument("--top", type=int, default=30)
        a = ap.parse_args(argv[1:])
        old = json.loads(Path(a.old).read_text())
        new = json.loads(Path(a.new).read_text())
        print("\n".join(diff_reports(old, new, a.top)))
        return 0
    if argv and argv[0] == "html":
        ap = argparse.ArgumentParser(prog="flash_map.py html", description="Render JSON reports.")
        ap.add_argument("json", nargs="+")
        ap.add_argument("-o", "--output", required=True)
        a = ap.parse_args(argv[1:])
        reps = [json.loads(Path(p).read_text()) for p in a.json]
        Path(a.output).write_text(build_html(reps), encoding="utf-8")
        return 0
    ap = argparse.ArgumentParser(
        description="Attribute the flash image of an ESP32 build to archives, objects and symbols "
        "using the GNU ld map file. Subcommands: 'diff OLD.json NEW.json', "
        "'html A.json [B.json ...] -o OUT.html'.")
    ap.add_argument("map", help="path to firmware.map")
    ap.add_argument("--elf", help="firmware.elf; adds exact ELF symbol sizes via the toolchain nm")
    ap.add_argument("--label", help="board label for the report (default: build dir name)")
    ap.add_argument("--json", help="write the full report as JSON to this file")
    ap.add_argument("--top", type=int, default=25, help="rows in the text summary (default 25)")
    ap.add_argument("--keep", type=int, default=400,
                    help="symbols/objects kept in the JSON (default 400)")
    ap.add_argument("--sections", help="comma-separated output sections that count as image "
                    "(default: the ESP32 flash/IRAM/DRAM-data/RTC/PHY set)")
    ap.add_argument("--no-tools", action="store_true", help="never call nm / c++filt")
    a = ap.parse_args(argv)
    rep = build_report(Path(a.map), label=a.label, top=a.keep,
                       elf=Path(a.elf) if a.elf else None,
                       sections=set(a.sections.split(",")) if a.sections else None,
                       use_tools=not a.no_tools)
    if a.json:
        Path(a.json).write_text(json.dumps(rep, indent=1) + "\n", encoding="utf-8")
    print_text(rep, a.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
