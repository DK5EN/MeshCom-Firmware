"""Tests for tools/au_stage_check.py (fwStageLayout port, QA-SIZE-AU01)."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import au_stage_check as sc  # noqa: E402

KB = 1024
SLOT = 3324 * KB


def test_layout_matches_c() -> None:
    assert sc.stage_layout(SLOT, 1000 * KB, 1100 * KB, 1000 * KB)[0] == sc.OK
    assert sc.stage_layout(SLOT, 1, 0, 1)[0] == sc.REF_SIZE
    assert sc.stage_layout(SLOT, 1, SLOT + 1, 1)[0] == sc.REF_SIZE
    assert sc.stage_layout(SLOT, 1, SLOT - 0x8000, 1)[0] == sc.REF_SMALL
    # off = (SLOT - z) & ~0xFFFF; running == off is fine, off + 1 overlaps
    res, off = sc.stage_layout(SLOT, 0, 1000 * KB, 1)
    assert res == sc.OK and off % 0x10000 == 0 and off > SLOT - 1000 * KB - 0x10000
    assert sc.stage_layout(SLOT, off, 1000 * KB, off)[0] == sc.OK
    assert sc.stage_layout(SLOT, off + 1, 1000 * KB, 1)[0] == sc.REF_OVERLAP
    assert sc.stage_layout(SLOT, 1, 1000 * KB, off + 1)[0] == sc.REF_INFLATE
    assert sc.stage_layout(SLOT, 1, 1000 * KB, 0)[0] == sc.REF_INFLATE


def test_max_growth_is_the_last_ok_size() -> None:
    size, z = 1900 * KB, 1100 * KB
    g = sc.max_growth(SLOT, size, z)
    assert g > 0

    def res(gg: int) -> int:
        return sc.stage_layout(SLOT, size + gg, round(z * (size + gg) / size), size + gg)[0]

    assert res(g) == sc.OK and res(g + 1) != sc.OK
    assert sc.max_growth(SLOT, SLOT, z) == -1
    assert sc.max_growth(SLOT, 0, z) == -1  # 0-byte .bin: refused, no ZeroDivisionError


def test_slot_from_csv_and_bin(tmp_path: Path) -> None:
    csv = tmp_path / "p.csv"
    csv.write_text("# Name,Type,SubType,Offset,Size,Flags\n"
                   "app, app, ota_0, 768K, 3324K,\n")
    assert sc.slot_from_csv(csv) == SLOT
    # real layout: entry bytes start AA 50 (LE u16 0x50AA), nvs first, MD5 entry (EB EB) last
    nvs = b"\xaa\x50" + struct.pack("<BBII16sI", 1, 2, 0x9000, 0x5000, b"nvs", 0)
    ent = b"\xaa\x50" + struct.pack("<BBII16sI", 0, 0x10, 0x10000, SLOT, b"app0", 0)
    pb = tmp_path / "partitions.bin"
    pb.write_bytes(nvs + ent + b"\xeb\xeb" + b"\xff" * 30)
    assert sc.slot_from_bin(pb) == SLOT
    pb.write_bytes(nvs + b"\xeb\xeb" + b"\xff" * 30)
    try:
        sc.slot_from_bin(pb)
        raise AssertionError("expected ValueError")
    except ValueError:
        pass
    # bad table in a build dir: exit 2, no traceback
    _env(tmp_path / "c", "c", 1900 * KB, 1100 * KB)
    (tmp_path / "c" / "partitions.bin").write_bytes(b"\x00" * 64)
    assert sc.main([str(tmp_path / "c")]) == 2


def _env(d: Path, name: str, size: int, z: int) -> None:
    d.mkdir(exist_ok=True)
    (d / f"{name}.bin").write_bytes(b"\xe9" * size)
    (d / f"{name}.autoupdate").write_bytes(b"z" * z)


def test_main_exit_codes(tmp_path: Path, capsys) -> None:
    _env(tmp_path / "a", "a", 1900 * KB, 1100 * KB)
    assert sc.main([str(tmp_path / "a")]) == 0
    assert "max growth" in capsys.readouterr().out
    _env(tmp_path / "b", "b", 2400 * KB, 1400 * KB)  # off too small for the running image
    assert sc.main([str(tmp_path / "a"), str(tmp_path / "b")]) == 1
    assert "REFUSED" in capsys.readouterr().out
    # explicit files; --slot-kb makes the tight env fit
    assert sc.main([str(tmp_path / "b" / "b.bin"), "--slot-kb", str(12288)]) == 0
    assert sc.main([str(tmp_path / "nope")]) == 2
