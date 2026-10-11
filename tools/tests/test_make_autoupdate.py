"""Tests for tools/make_autoupdate.py (AU-D11 compressed release assets)."""

from __future__ import annotations

import hashlib
import json
import random
import sys
import zlib
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import make_autoupdate as mz  # noqa: E402


def fake_image(n: int = 70_000, seed: int = 1) -> bytes:
    """0xE9 magic, then a mix of compressible and random bytes, > 32 KB."""
    rng = random.Random(seed)
    body = bytearray()
    while len(body) < n:
        body += bytes(rng.randrange(256) for _ in range(300))
        body += b"MeshCom" * 200
        body += bytes(500)
    return bytes([0xE9]) + bytes(body[: n - 1])


@pytest.fixture
def img(tmp_path: Path) -> Path:
    p = tmp_path / "heltec.bin"
    p.write_bytes(fake_image())
    return p


def test_round_trip_and_name(img: Path, tmp_path: Path) -> None:
    out = tmp_path / "out"
    assert mz.main([str(img), "--out-dir", str(out), "--check"]) == 0
    zz = out / "heltec.autoupdate"
    assert zz.is_file()
    assert zlib.decompress(zz.read_bytes()) == img.read_bytes()
    assert len(zz.read_bytes()) < img.stat().st_size


def test_default_out_dir_is_next_to_input(img: Path) -> None:
    assert mz.main([str(img)]) == 0
    assert (img.parent / "heltec.autoupdate").is_file()


def test_zlib_header_and_window(img: Path, tmp_path: Path) -> None:
    mz.main([str(img), "--out-dir", str(tmp_path)])
    z = (tmp_path / "heltec.autoupdate").read_bytes()
    assert z[:2] == b"\x78\xda"
    # CINFL = 7 => 2^(7+8) = 32 KB window; header checksum valid.
    assert (z[0] >> 4) == 7 and z[0] & 0x0F == 8
    assert ((z[0] << 8) | z[1]) % 31 == 0
    # Inflates with exactly a 32 KB window (wbits 15), not smaller.
    assert zlib.decompressobj(15).decompress(z) == img.read_bytes()
    assert mz.ZLIB_WBITS == 15


def test_stream_fits_tinfl_dictionary(tmp_path: Path) -> None:
    """Match distances never exceed 32 KB: a 15-bit-window inflate succeeds on > 32 KB input."""
    data = fake_image(200_000, seed=7)
    z = mz.compress(data)
    assert mz.inflate(z) == data


def test_refuses_non_image_without_force(tmp_path: Path) -> None:
    bad = tmp_path / "notimg.bin"
    bad.write_bytes(b"\x00" + b"x" * 100)
    assert mz.main([str(bad), "--out-dir", str(tmp_path / "o")]) == 1
    assert not (tmp_path / "o" / "notimg.autoupdate").exists()


def test_force_accepts_non_image(tmp_path: Path) -> None:
    bad = tmp_path / "raw.bin"
    bad.write_bytes(b"\x00" + b"x" * 100)
    assert mz.main([str(bad), "--out-dir", str(tmp_path), "--force", "--check"]) == 0
    assert (tmp_path / "raw.autoupdate").is_file()


def test_refuses_empty_and_missing(tmp_path: Path) -> None:
    empty = tmp_path / "e.bin"
    empty.write_bytes(b"")
    assert mz.main([str(empty), "--force"]) == 1
    assert mz.main([str(tmp_path / "nope.bin")]) == 1


def test_one_bad_input_does_not_stop_the_rest(img: Path, tmp_path: Path) -> None:
    bad = tmp_path / "bad.bin"
    bad.write_bytes(b"\x01abc")
    rc = mz.main([str(bad), str(img), "--out-dir", str(tmp_path / "o")])
    assert rc == 1
    assert (tmp_path / "o" / "heltec.autoupdate").is_file()


def test_check_detects_corruption(
    img: Path, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    real = mz.compress

    def broken(data: bytes) -> bytes:
        z = bytearray(real(data))
        z[len(z) // 2] ^= 0xFF
        return bytes(z)

    monkeypatch.setattr(mz, "compress", broken)
    assert mz.main([str(img), "--out-dir", str(tmp_path / "o"), "--check"]) == 1


def test_manifest_content(img: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    second = tmp_path / "t_deck.bin"
    second.write_bytes(fake_image(40_000, seed=2))
    man = tmp_path / "m.json"
    out = tmp_path / "o"
    rc = mz.main([str(img), str(second), "--out-dir", str(out), "--manifest", str(man), "--check"])
    assert rc == 0
    entries = json.loads(man.read_text())
    assert [e["asset"] for e in entries] == ["heltec.autoupdate", "t_deck.autoupdate"]
    for e, src in zip(entries, (img, second), strict=True):
        raw = src.read_bytes()
        zz = (out / e["asset"]).read_bytes()
        assert set(e) == {"asset", "size", "zsize", "sha256_bin", "sha256_zz"}
        assert e["size"] == len(raw) and e["zsize"] == len(zz)
        assert e["sha256_bin"] == hashlib.sha256(raw).hexdigest()
        assert e["sha256_zz"] == hashlib.sha256(zz).hexdigest()
    line = capsys.readouterr().out.splitlines()[0].split()
    assert line[0] == "heltec.autoupdate" and line[4] == entries[0]["sha256_zz"]


def test_deterministic(img: Path) -> None:
    raw = img.read_bytes()
    assert mz.compress(raw) == mz.compress(raw)


def test_asset_name_drops_bin() -> None:
    assert mz.asset_name(Path("x/heltec_wifi_lora_32_V3.bin")) == "heltec_wifi_lora_32_V3.autoupdate"
    assert mz.asset_name(Path("raw")) == "raw.autoupdate"


# --- asset-name length: fwAssetName() buffers are 48 bytes (incl. NUL) ---------------------

REPO = TOOLS.parent
AU_SUFFIX = ".autoupdate"
AU_BUF = 48
RENAMES = {
    "LilyGo_T-Beam-1W": "T-Beam-1W",
    "LilyGo_T3_S3_V1_3": "T3_S3_V13",
    "LilyGo_T_Connect_Pro": "t_connect_pro",
}


def _envs() -> list[str]:
    import re

    files = [REPO / "platformio.ini", *sorted(REPO.glob("variants/*/platformio.ini"))]
    names: list[str] = []
    for f in files:
        for m in re.finditer(r"^\[env:([^\]]+)\]", f.read_text(), re.M):
            n = m.group(1)
            if n.startswith("native") or "safeboot" in n:
                continue
            names.append(n)
    return names


def test_every_env_asset_name_fits_buffer() -> None:
    envs = _envs()
    assert len(envs) > 10
    for env in envs:
        base = RENAMES.get(env, env)
        assert len(base + AU_SUFFIX) + 1 <= AU_BUF, env
