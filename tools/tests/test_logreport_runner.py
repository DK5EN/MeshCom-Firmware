"""Runner tests for tools/logreport/run.py (node keys, prefix canonicalisation, groups, render-only)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "logreport"))

import run  # noqa: E402


def test_node_keys() -> None:
    assert run.node_key(Path("meshcom_oe3xhl_2026-10-31_064400.log"), {}) == "oe3xhl"
    assert run.node_key(Path("HB9VQQ-4_loradebug_20261031-0939.log"), {}) == "hb9vqq4"
    assert run.node_key(Path("DB0AIS 24 Stnuden.log"), {}) == "db0ais"
    assert run.node_key(Path("x.log"), {"x.log": "custom"}) == "custom"


def test_normalize_canonicalises_one_space_prefix(tmp_path: Path) -> None:
    logs = tmp_path / "logs"
    logs.mkdir()
    (logs / "HB9VQQ-4_x.log").write_bytes(b"2026-10-31 09:39:56.035 [LOGGER] start\r\n"
                                          b"2026-10-31 09:39:57.001  [MC-DBG] two spaces kept\n"
                                          b"no prefix line\n")
    out = run.normalize(logs, tmp_path / "work", {})
    assert [p.name for p in out] == ["hb9vqq4.log"]
    assert out[0].read_bytes() == (b"2026-10-31 09:39:56.035  [LOGGER] start\n"
                                   b"2026-10-31 09:39:57.001  [MC-DBG] two spaces kept\n"
                                   b"no prefix line\n")


def test_groups_split_meshes(tmp_path: Path) -> None:
    norm = tmp_path / "work" / "norm"
    norm.mkdir(parents=True)
    for k in ("oe3xhl", "db0ais", "hb9vqq4", "hb9vqq5"):
        (norm / f"{k}.log").write_text("")
    runp = tmp_path / "run"
    runp.mkdir()
    (runp / "logreport.json").write_text(json.dumps({"groups": {"hb9": ["hb9vqq4", "hb9vqq5"]}}))
    g = run.groups(tmp_path / "work", runp)
    assert sorted(p.stem for p in g["main"]) == ["db0ais", "oe3xhl"]
    assert sorted(p.stem for p in g["hb9"]) == ["hb9vqq4", "hb9vqq5"]


def test_render_only_needs_no_logs(tmp_path: Path) -> None:
    """Regression: --steps render without --logs crashed computing the work dir (None / str)."""
    runp = tmp_path / "run"
    (runp / "generators").mkdir(parents=True)
    (runp / "data").mkdir()
    (runp / "generators" / "page.py").write_text(
        "import argparse\nfrom pathlib import Path\n"
        "ap = argparse.ArgumentParser()\nap.add_argument('--data')\nap.add_argument('--kit')\nap.add_argument('--out')\n"
        "a = ap.parse_args()\nPath(a.out).write_text('<!DOCTYPE html><title>t</title>')\n")
    assert run.main(["--run", str(runp), "--steps", "render"]) == 0
    assert (runp / "page.html").read_text().startswith("<!DOCTYPE html>")
