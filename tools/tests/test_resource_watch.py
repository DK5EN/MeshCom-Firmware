"""Tests for tools/resource_watch.py: RAM budget gate (OTA-LOWHEAP-01) and baseline refresh."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "resource_watch.py"


def ram_line(used: int, total: int = 327680) -> str:
    return f"RAM:   [==        ]  23.4% (used {used} bytes from {total} bytes)"


def section(env: str, used: int) -> str:
    return (
        f"Processing {env} (platform: espressif32; board: x; framework: arduino)\n"
        "--------------------------------------------------------------------\n"
        "Building in release mode\n"
        f"{ram_line(used)}\n"
        "Flash: [==========]  43.9% (used 1492589 bytes from 3403776 bytes)\n"
        f"========================= [SUCCESS] Took 1.0 seconds =========================\n"
    )


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args], capture_output=True, text=True, check=False
    )


def make_baseline(tmp_path: Path, envs: dict[str, int]) -> Path:
    p = tmp_path / "baseline.json"
    p.write_text(json.dumps({e: {"ram_used": u, "ram_total": 327680, "flash_used": 1, "flash_total": 2}
                             for e, u in envs.items()}))
    return p


def write_log(tmp_path: Path, envs: dict[str, int]) -> Path:
    p = tmp_path / "build.log"
    p.write_text("".join(section(e, u) for e, u in envs.items()))
    return p


def test_parse_ram_line() -> None:
    sys.path.insert(0, str(ROOT / "tools"))
    import resource_watch as rw

    u = rw.parse_usage(ram_line(76032) + "\nFlash: [=] 1.0% (used 5 bytes from 10 bytes)\n")
    assert u["ram_used"] == 76032 and u["ram_total"] == 327680


def test_within_budget_passes(tmp_path: Path) -> None:
    base = make_baseline(tmp_path, {"a": 100000, "b": 50000})
    log = write_log(tmp_path, {"a": 101024, "b": 49000})  # exactly +1024, and a shrink
    r = run("gate", "--log", str(log), "--baseline", str(base), "--build-dir", str(tmp_path / "nobuild"))
    assert r.returncode == 0, r.stdout + r.stderr
    assert "+1024" in r.stdout


def test_over_budget_fails(tmp_path: Path) -> None:
    base = make_baseline(tmp_path, {"a": 100000})
    log = write_log(tmp_path, {"a": 101025})
    r = run("gate", "--log", str(log), "--baseline", str(base), "--build-dir", str(tmp_path / "nobuild"))
    assert r.returncode == 1
    assert "::error" in r.stdout + r.stderr and "a" in r.stdout


def test_missing_env_fails(tmp_path: Path) -> None:
    base = make_baseline(tmp_path, {"a": 100000})
    log = write_log(tmp_path, {"a": 100000, "newboard": 1000})
    r = run("gate", "--log", str(log), "--baseline", str(base), "--build-dir", str(tmp_path / "nobuild"))
    assert r.returncode == 1
    out = r.stdout + r.stderr
    assert "newboard" in out and "no RAM line baseline" in out and "resource_watch.py update" in out


def test_summary_file_written(tmp_path: Path) -> None:
    base = make_baseline(tmp_path, {"a": 100000})
    log = write_log(tmp_path, {"a": 100500})
    summ = tmp_path / "summary.md"
    r = run("gate", "--log", str(log), "--baseline", str(base), "--summary", str(summ), "--build-dir", str(tmp_path / "nobuild"))
    assert r.returncode == 0
    assert "| a | RAM line | 100000 | 100500 | +500 |" in summ.read_text()


def test_update_writes_baseline_from_combined_log(tmp_path: Path) -> None:
    base = tmp_path / "new.json"
    log = write_log(tmp_path, {"a": 111, "b": 222})
    r = run("update", "--log", str(log), "--baseline", str(base))
    assert r.returncode == 0, r.stdout + r.stderr
    data = json.loads(base.read_text())
    assert data["a"]["ram_used"] == 111 and data["b"]["ram_used"] == 222
    assert data["a"]["flash_used"] == 1492589


FLASH = "Flash: [==========]  43.9% (used 1492589 bytes from 3403776 bytes)"


def test_gate_reads_real_ci_header_format(tmp_path: Path) -> None:
    # Real PlatformIO/gh-log headers: "<job>\t<step>\t<ts> Processing <env> (board: ...; platform: ...)".
    # The first gate version matched only "(platform:" and saw 5 of 36 envs in CI run 44939256914.
    prefix = "build\tUNKNOWN STEP\t2026-11-21T19:09:09.8134243Z "
    log = tmp_path / "ci.log"
    log.write_text(
        prefix + "Processing heltec_wifi_lora_32_V3 (board: heltec_wifi_lora_32_V3; framework: arduino; "
        "platform: https://example/p.zip)\n" + prefix + ram_line(137052) + "\n" + prefix + FLASH + "\n"
        + prefix + "Processing ttgo_tbeam (platform: espressif32@^6.13.0; board: ttgo-t-beam)\n"
        + prefix + ram_line(107080) + "\n" + prefix + FLASH + "\n"
    )
    bl = make_baseline(tmp_path, {"heltec_wifi_lora_32_V3": 137052, "ttgo_tbeam": 107080})
    r = run("gate", "--log", str(log), "--baseline", str(bl), "--build-dir", str(tmp_path / "nobuild"))
    assert r.returncode == 0, r.stdout + r.stderr
    assert "heltec_wifi_lora_32_V3" in r.stdout and "ttgo_tbeam" in r.stdout


# --- linker regions (operator 2026-11-22: gate on dram0_0_seg / iram0_0_seg, not on the RAM: summary line) ---

def fake_map(dram_used: int, iram_used: int) -> str:
    # Minimal ESP32 map: memory configuration lines plus the two end symbols parse_region reads.
    d0, i0 = 0x3FC88000, 0x40370000
    return (
        "Memory Configuration\n\n"
        f"iram0_0_seg      0x{i0:08x}         0x{0x20000:08x}         xrw\n"
        f"dram0_0_seg      0x{d0:08x}         0x{0x50000:08x}         rw\n\n"
        f"                0x{d0 + dram_used:08x}                _bss_end = ABSOLUTE (.)\n"
        f"                0x{i0 + iram_used:08x}                _iram_end = ABSOLUTE (.)\n"
    )


def write_build(tmp_path: Path, env: str, dram: int, iram: int) -> Path:
    bdir = tmp_path / "build"
    (bdir / env).mkdir(parents=True, exist_ok=True)
    (bdir / env / "firmware.map").write_text(fake_map(dram, iram))
    return bdir


def region_baseline(tmp_path: Path, env: str, dram: int, iram: int) -> Path:
    p = tmp_path / "baseline.json"
    p.write_text(json.dumps({env: {"ram_used": 1, "ram_total": 2, "flash_used": 1, "flash_total": 2,
                                   "dram0_0_seg": {"used": dram, "length": 0x50000},
                                   "iram0_0_seg": {"used": iram, "length": 0x20000}}}))
    return p


def test_gate_uses_dram_region_not_summary_line(tmp_path: Path) -> None:
    log = tmp_path / "b.log"
    log.write_text(section("heltec_wifi_lora_32_V3", 1))  # summary line unchanged vs baseline
    bdir = write_build(tmp_path, "heltec_wifi_lora_32_V3", 100000 + 1025, 60000)
    bl = region_baseline(tmp_path, "heltec_wifi_lora_32_V3", 100000, 60000)
    r = run("gate", "--log", str(log), "--baseline", str(bl), "--build-dir", str(bdir))
    assert r.returncode == 1, r.stdout
    assert "dram0_0_seg" in r.stdout and "+1025" in r.stdout


def test_gate_counts_iram_growth(tmp_path: Path) -> None:
    log = tmp_path / "b.log"
    log.write_text(section("heltec_wifi_lora_32_V3", 1))
    bdir = write_build(tmp_path, "heltec_wifi_lora_32_V3", 100000, 60000 + 2000)
    bl = region_baseline(tmp_path, "heltec_wifi_lora_32_V3", 100000, 60000)
    r = run("gate", "--log", str(log), "--baseline", str(bl), "--build-dir", str(bdir))
    assert r.returncode == 1 and "iram0_0_seg" in r.stdout


def test_gate_regions_within_budget_pass(tmp_path: Path) -> None:
    log = tmp_path / "b.log"
    log.write_text(section("heltec_wifi_lora_32_V3", 999999))  # summary line ignored for ESP32 maps
    bdir = write_build(tmp_path, "heltec_wifi_lora_32_V3", 101024, 61024)
    bl = region_baseline(tmp_path, "heltec_wifi_lora_32_V3", 100000, 60000)
    r = run("gate", "--log", str(log), "--baseline", str(bl), "--build-dir", str(bdir))
    assert r.returncode == 0, r.stdout


def test_gate_missing_region_baseline_fails(tmp_path: Path) -> None:
    log = tmp_path / "b.log"
    log.write_text(section("heltec_wifi_lora_32_V3", 1))
    bdir = write_build(tmp_path, "heltec_wifi_lora_32_V3", 100000, 60000)
    bl = make_baseline(tmp_path, {"heltec_wifi_lora_32_V3": 1})  # summary only, no regions
    r = run("gate", "--log", str(log), "--baseline", str(bl), "--build-dir", str(bdir))
    assert r.returncode == 1 and "no dram0_0_seg baseline" in r.stdout


def test_gate_esp32_baseline_but_map_missing_fails(tmp_path: Path) -> None:
    log = tmp_path / "b.log"
    log.write_text(section("heltec_wifi_lora_32_V3", 1))
    bl = region_baseline(tmp_path, "heltec_wifi_lora_32_V3", 100000, 60000)
    r = run("gate", "--log", str(log), "--baseline", str(bl), "--build-dir", str(tmp_path / "nobuild"))
    assert r.returncode == 1 and "firmware.map" in r.stdout


def test_update_reads_regions_from_build_dir(tmp_path: Path) -> None:
    log = tmp_path / "b.log"
    log.write_text(section("heltec_wifi_lora_32_V3", 137052))
    bdir = write_build(tmp_path, "heltec_wifi_lora_32_V3", 123456, 65432)
    bl = tmp_path / "bl.json"
    bl.write_text("{}")
    r = run("update", "--log", str(log), "--baseline", str(bl), "--build-dir", str(bdir))
    assert r.returncode == 0, r.stderr
    e = json.loads(bl.read_text())["heltec_wifi_lora_32_V3"]
    assert e["dram0_0_seg"]["used"] == 123456 and e["iram0_0_seg"]["used"] == 65432 and e["ram_used"] == 137052
