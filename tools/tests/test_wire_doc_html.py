"""Tests for tools/wire_doc_html.py (run: uv run --with pytest --with markdown-it-py pytest)."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "wire_doc_html.py"
spec = importlib.util.spec_from_file_location("wire_doc_html", SCRIPT)
assert spec and spec.loader
wd = importlib.util.module_from_spec(spec)
sys.modules["wire_doc_html"] = wd
spec.loader.exec_module(wd)

SAMPLE = (
    "# Doc Title — the sub line\n\nStatus: written 2026-01-02 against firmware 4.40a\n\n"
    "## 1. First\n\n### 1.1 Byte 5 — flags + hop\n\ntext <script>x</script> `a&b`\n\n"
    "| k | v |\n|---|---|\n| `<` | 1 |\n"
)


def test_slugs() -> None:
    assert wd.slugify("1.2 Byte 5 — flags + hop") == "1-2-byte-5-flags-hop"
    assert wd.slugify("2.3 :sto and STOR") == "2-3-sto-and-stor"
    assert wd.slugify("1.8 Tail: the /X= key space") == "1-8-tail-the-x-key-space"


def test_duplicate_slug_suffix() -> None:
    _, _, toc, _ = wd.render_markdown("# T\n\n## A\n\n## A\n")
    assert [t[1] for t in toc] == ["a", "a-2"]


def test_escaping() -> None:
    page = wd.render_page(SAMPLE.encode(), "x.md", wd.TEMPLATE.read_text("utf-8"))
    assert "<script>x</script>" not in page.split("<main>")[1].split("</main>")[0]
    assert "&lt;script&gt;" in page
    assert "<code>a&amp;b</code>" in page
    assert '<div class="tbl"><table>' in page


def test_sha_meta_and_title() -> None:
    page = wd.render_page(SAMPLE.encode(), "x.md", wd.TEMPLATE.read_text("utf-8"))
    sha = hashlib.sha256(SAMPLE.encode()).hexdigest()
    assert f'<meta name="source-sha256" content="{sha}">' in page
    assert "<title>Doc Title</title>" in page
    assert '<p class="sub">the sub line</p>' in page
    assert 'href="#1-1-byte-5-flags-hop"' in page


def test_deterministic() -> None:
    tpl = wd.TEMPLATE.read_text("utf-8")
    assert wd.render_page(SAMPLE.encode(), "x.md", tpl) == wd.render_page(
        SAMPLE.encode(), "x.md", tpl
    )


def test_check_detects_change(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    md = tmp_path / "d.md"
    out = tmp_path / "d.html"
    md.write_text(SAMPLE, encoding="utf-8")
    assert wd.main(["--md", str(md), "--html", str(out)]) == 0
    assert wd.main(["--md", str(md), "--html", str(out), "--check"]) == 0
    md.write_text(SAMPLE + "\nmore\n", encoding="utf-8")
    assert wd.main(["--md", str(md), "--html", str(out), "--check"]) == 1
    assert "STALE" in capsys.readouterr().err


def test_self_test() -> None:
    assert wd.main(["--self-test"]) == 0
