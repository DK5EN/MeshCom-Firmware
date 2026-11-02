"""Tests for tools/verify_release_assets.py (QA-AU-05)."""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import make_autoupdate as mau  # noqa: E402
import verify_release_assets as vra  # noqa: E402


def make_image(env: str, tag: str = "v1") -> bytes:
    img = bytearray(0x300)
    img[0] = 0xE9
    img[0x20:0x24] = (0xABCD5432).to_bytes(4, "little")
    blob = bytearray(128)
    blob[:8] = b"MCBOARD1"
    blob[8:10] = (1).to_bytes(2, "little")
    blob[12 : 12 + len(env)] = env.encode()
    blob[44 : 44 + len(tag)] = tag.encode()
    img[0x120:0x1A0] = blob
    return bytes(img)


def put(d: Path, name: str, img: bytes) -> None:
    (d / f"{name}.bin").write_bytes(img)
    (d / f"{name}.autoupdate").write_bytes(mau.compress(img))


def test_ok_including_renamed_asset(tmp_path: Path) -> None:
    put(tmp_path, "t_deck", make_image("t_deck"))
    put(tmp_path, "T-Beam-1W", make_image("LilyGo_T-Beam-1W"))
    assert vra.verify_dir(tmp_path, "v1", 2, 4) == []
    assert vra.main([str(tmp_path), "--tag", "v1", "--pairs", "2"]) == 0


def test_detects_problems(tmp_path: Path) -> None:
    put(tmp_path, "a", make_image("a"))
    put(tmp_path, "b", make_image("other"))  # marker env != file name
    put(tmp_path, "c", make_image("c", "v0"))  # stale tag
    put(tmp_path, "d", make_image("d"))
    (tmp_path / "d.autoupdate").write_bytes(mau.compress(make_image("d") + b"x"))  # pair differs
    (tmp_path / "e.autoupdate").write_bytes(b"x")  # .bin missing
    out = "\n".join(vra.verify_dir(tmp_path, "v1", 4, None))
    for needle in ("5 .autoupdate", "b: marker env", "c: marker tag", "d: ", "e: e.bin missing"):
        assert needle in out
    assert "a:" not in out
    assert vra.main([str(tmp_path), "--total", "1"]) == 1
