#!/usr/bin/env python3
"""Freeze the LEGACY gateway-server selection of the three code paths into a golden JSON.

This file is generated ONCE from the pre-refactor commit and never regenerated from new code
(docs/gw-servers-unification.md section 3.5). The legacy regions are cut out of the old commit by
text markers (git show, never the working tree), compiled unchanged against the stubs in
tools/gw_legacy_harness.h with the host C++ compiler and executed over a grid.

Usage:
  uv run tools/gw_legacy_capture.py --commit fe8f10d0 --out test/golden/gw_servers_legacy.json
  uv run tools/gw_legacy_capture.py --check [--golden FILE]   # regenerate in memory and compare
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL_NAME = "tools/gw_legacy_capture.py"
DEFAULT_COMMIT = "fe8f10d0"
DEFAULT_GOLDEN = ROOT / "test" / "golden" / "gw_servers_legacy.json"

PLATFORMS = ["esp32", "nrf_dyn", "nrf_static"]
CODES = ["OE", "DL", "IT", "HC", "HB", "XX", ""]
OWN_IPS = ["44.143.1.5", "44.148.1.5", "44.142.1.5", "10.0.0.5", "192.168.1.5"]
HAMNET_ONLY = [0, 1, 2, 7]
OWN_NTPS = ["", "10.9.8.7", "ntp.example.org"]
NA = "n/a"  # nRF paths ignore own NTP: run once


class ExtractionError(Exception):
    """A marker is missing or ambiguous in the legacy source."""


def _lines_with(lines: list[str], marker: str, start: int = 0) -> list[int]:
    return [i for i in range(start, len(lines)) if marker in lines[i]]


def extract_region(text: str, start_marker: str, end_marker: str, what: str) -> str:
    """Lines from the single line containing start_marker up to (excluding) the first later
    line containing end_marker."""
    lines = text.splitlines()
    starts = _lines_with(lines, start_marker)
    if len(starts) != 1:
        raise ExtractionError(
            f"{what}: start marker {start_marker!r} found {len(starts)} times (need exactly 1)"
        )
    ends = _lines_with(lines, end_marker, starts[0] + 1)
    if not ends:
        raise ExtractionError(f"{what}: end marker {end_marker!r} not found after start")
    return "\n".join(lines[starts[0] : ends[0]]) + "\n"


def extract_all(esp_text: str, nrf_text: str) -> dict[str, str]:
    return {
        "esp32": extract_region(
            esp_text, "const char *srv_host = NULL;", "if(ntp_host == NULL)", "esp32"
        ),
        "nrf_dyn": extract_region(
            nrf_text,
            "const char *srv_path = NULL;   // TM-39",
            "#if INSTRUMENT_ENABLED",
            "nrf_dyn",
        ),
        "nrf_static": extract_region(
            nrf_text,
            'const char *srv_path = "inet";',
            'snprintf(sn, sizeof(sn), "%i.%i.%i.%i", udp_dest_addr',
            "nrf_static",
        ),
    }


def git_show(commit: str, path: str) -> str:
    r = subprocess.run(
        ["git", "show", f"{commit}:{path}"], cwd=ROOT, capture_output=True, text=True, check=False
    )
    if r.returncode != 0:
        raise SystemExit(f"git show {commit}:{path} failed: {r.stderr.strip()}")
    return r.stdout


def build_source(regions: dict[str, str]) -> str:
    return f"""#include "gw_legacy_harness.h"
#include <iostream>
#include <sstream>
#include <string>

static std::string dotted(const IPAddress &a)
{{
  char b[24];
  snprintf(b, sizeof(b), "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
  return b;
}}
static std::string js(const char *s) {{ return s ? std::string("\\"") + s + "\\"" : "null"; }}
static std::string jd(const std::string &s) {{ return "\\"" + s + "\\""; }}

static void run_esp32(const IPAddress &node_ip)
{{
{regions["esp32"]}
  std::string ntp = ntp_host ? js(ntp_host) : jd(dotted(ntp_literal));
  std::cout << js(srv_host) << "\\t" << ntp << "\\t" << js(srv_path) << "\\n";
}}

static void run_nrf_dyn(const IPAddress &local_addr)
{{
{regions["nrf_dyn"]}
  std::string ntp = timeClient.calls ? jd(dotted(timeClient.last)) : "null";
  std::cout << jd(dotted(udp_dest_addr)) << "\\t" << ntp << "\\t" << js(srv_path) << "\\n";
}}

static void run_nrf_static()
{{
{regions["nrf_static"]}
  std::string ntp = timeClient.calls ? jd(dotted(timeClient.last)) : "null";
  std::cout << jd(dotted(udp_dest_addr)) << "\\t" << ntp << "\\t" << js(srv_path) << "\\n";
}}

int main()
{{
  std::string plat, code, ip, ham, ntp;
  while (std::cin >> plat >> code >> ip >> ham >> ntp)
  {{
    memset(&meshcom_settings, 0, sizeof(meshcom_settings));
    udp_dest_addr = IPAddress();
    timeClient = TimeClientStub();
    if (code != "-") strncpy(meshcom_settings.node_gwsrv, code.c_str(), 2);
    meshcom_settings.node_hamnet_only = atoi(ham.c_str());
    if (ntp != "-") strncpy(meshcom_settings.node_ntp, ntp.c_str(), sizeof(meshcom_settings.node_ntp) - 1);
    strncpy(meshcom_settings.node_ip, ip.c_str(), sizeof(meshcom_settings.node_ip) - 1);
    int a, b, c, d;
    sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d);
    IPAddress addr(a, b, c, d);
    if (plat == "esp32") run_esp32(addr);
    else if (plat == "nrf_dyn") run_nrf_dyn(addr);
    else run_nrf_static();
  }}
  return 0;
}}
"""


def grid() -> list[tuple[str, str, str, int, str]]:
    rows = []
    for plat in PLATFORMS:
        ntps = OWN_NTPS if plat == "esp32" else [NA]
        for code in CODES:
            for ip in OWN_IPS:
                for ham in HAMNET_ONLY:
                    for ntp in ntps:
                        rows.append((plat, code, ip, ham, ntp))
    return sorted(rows)


def run_grid(regions: dict[str, str]) -> list[dict]:
    rows = grid()
    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        (tdp / "gw_legacy_harness.h").write_text((ROOT / "tools" / "gw_legacy_harness.h").read_text())
        (tdp / "main.cpp").write_text(build_source(regions))
        exe = tdp / "legacy"
        cc = subprocess.run(
            ["c++", "-std=gnu++17", "-w", "-I", str(tdp), str(tdp / "main.cpp"), "-o", str(exe)],
            capture_output=True,
            text=True,
            check=False,
        )
        if cc.returncode != 0:
            raise SystemExit(f"compile of the legacy regions failed:\n{cc.stderr}")
        stdin = "".join(
            f"{p} {c or '-'} {ip} {h} {(n if n != NA and n else '-')}\n" for p, c, ip, h, n in rows
        )
        out = subprocess.run(
            [str(exe)], input=stdin, capture_output=True, text=True, check=True
        ).stdout.splitlines()
    if len(out) != len(rows):
        raise SystemExit(f"harness produced {len(out)} lines for {len(rows)} rows")
    result = []
    for (p, c, ip, h, n), line in zip(rows, out, strict=True):
        srv, ntp, path = (json.loads(x) for x in line.split("\t"))
        result.append(
            {
                "platform": p,
                "code": c,
                "own_ip": ip,
                "hamnet_only": h,
                "own_ntp": n,
                "srv": srv,
                "ntp": ntp,
                "path": path,
            }
        )
    return result


def render(commit: str, rows: list[dict]) -> str:
    header = {
        "commit": commit,
        "tool": TOOL_NAME,
        "note": "generated once from the pre-refactor commit by executing the legacy code; "
        "never regenerate from new code",
        "sources": {
            "esp32": "src/udp_functions.cpp startMeshComUDP()",
            "nrf_dyn": "src/nrf52/nrf_eth.cpp NrfETH::startUDP()",
            "nrf_static": "src/nrf52/nrf_eth.cpp NrfETH::startFIXUDP()",
        },
        "grid": {
            "platform": PLATFORMS,
            "code": CODES,
            "own_ip": OWN_IPS,
            "hamnet_only": HAMNET_ONLY,
            "own_ntp": {"esp32": OWN_NTPS, "nrf_dyn": [NA], "nrf_static": [NA]},
        },
        "fields": "srv/ntp/path as the legacy code leaves them; null = the legacy left it unset",
    }
    body = ",\n".join("  " + json.dumps(r, sort_keys=True) for r in rows)
    return json.dumps({"header": header}, sort_keys=True, indent=1)[:-2] + (
        f',\n "rows": [\n{body}\n ]\n}}\n'
    )


def generate(commit: str) -> str:
    regions = extract_all(
        git_show(commit, "src/udp_functions.cpp"), git_show(commit, "src/nrf52/nrf_eth.cpp")
    )
    return render(commit, run_grid(regions))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--commit", default=None)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--golden", type=Path, default=DEFAULT_GOLDEN)
    a = ap.parse_args()
    try:
        if a.check:
            committed = a.golden.read_text()
            commit = a.commit or json.loads(committed)["header"]["commit"]
            if generate(commit) == committed:
                print(f"OK: {a.golden} matches legacy commit {commit}")
                return 0
            print(f"MISMATCH: {a.golden} differs from regenerated legacy output", file=sys.stderr)
            return 1
        if not a.out:
            ap.error("--out is required unless --check")
        a.out.write_text(generate(a.commit or DEFAULT_COMMIT))
        print(f"wrote {a.out}")
        return 0
    except ExtractionError as e:
        print(f"EXTRACTION FAILED: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
