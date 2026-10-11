# /// script
# requires-python = ">=3.10"
# dependencies = ["markdown-it-py==3.0.0"]
# ///
"""Render docs/architecture/11-wire-format.md into 11-wire-format.html.

The Markdown file is the source of truth. The page shell (head, CSS, mast,
TOC container, scroll script) lives in tools/wire_doc_template.html.

    uv run tools/wire_doc_html.py            regenerate the HTML
    uv run tools/wire_doc_html.py --check    exit 1 if the committed HTML is stale
    uv run tools/wire_doc_html.py --self-test
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import html
import re
import sys
from pathlib import Path
from typing import Any

from markdown_it import MarkdownIt
from markdown_it.token import Token

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
DEFAULT_MD = REPO_ROOT / "docs" / "architecture" / "11-wire-format.md"
DEFAULT_HTML = REPO_ROOT / "docs" / "architecture" / "11-wire-format.html"
TEMPLATE = TOOLS_DIR / "wire_doc_template.html"

VECTORS_META = "test/test_aprs_corpus · pio test -e native_aprs"


def slugify(text: str) -> str:
    """Lower-case, every run of non-alphanumerics becomes one hyphen."""
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def _heading_text(inline: Token) -> str:
    parts: list[str] = []
    for child in inline.children or []:
        if child.type in ("text", "code_inline"):
            parts.append(child.content)
    return "".join(parts)


def _render_table_open(self: Any, tokens: list[Token], idx: int, options: Any, env: Any) -> str:
    return '<div class="tbl"><table>\n'


def _render_table_close(self: Any, tokens: list[Token], idx: int, options: Any, env: Any) -> str:
    return "</table></div>\n"


def make_parser() -> MarkdownIt:
    md = MarkdownIt("commonmark", {"html": False}).enable("table")
    md.add_render_rule("table_open", _render_table_open)
    md.add_render_rule("table_close", _render_table_close)
    return md


def render_markdown(source: str) -> tuple[str, str, list[tuple[int, str, str]], str]:
    """Return (h1 text, body html, toc entries (level, id, inner html), first-para)."""
    md = make_parser()
    tokens = md.parse(source)
    env: dict[str, Any] = {}
    toc: list[tuple[int, str, str]] = []
    seen: dict[str, int] = {}
    h1 = ""
    body_tokens: list[Token] = []
    skip_h1 = True
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok.type == "heading_open":
            inline = tokens[i + 1]
            level = int(tok.tag[1])
            if level == 1 and skip_h1:
                h1 = _heading_text(inline)
                skip_h1 = False
                i += 3
                continue
            slug = slugify(_heading_text(inline)) or "section"
            n = seen.get(slug, 0)
            seen[slug] = n + 1
            if n:
                slug = f"{slug}-{n + 1}"
            tok.attrSet("id", slug)
            if level in (2, 3):
                inner = md.renderer.renderInline(inline.children or [], md.options, env)
                toc.append((level, slug, inner))
        body_tokens.append(tok)
        i += 1
    body = md.renderer.render(body_tokens, md.options, env)
    return h1, body, toc, ""


def build_meta(source: str) -> str:
    items: list[tuple[str, str]] = []
    m = re.search(r"written (\d{4}-\d{2}-\d{2}) against firmware ([\w.]+)", source)
    if m:
        items.append(("Baseline", f"{m.group(2)}, {m.group(1)}"))
    dates = re.findall(r"^Addendum[^\n]*?(\d{4}-\d{2}-\d{2})", source, re.M)
    if dates:
        items.append(("Last addendum", max(dates)))
    items.append(("Vectors", VECTORS_META))
    return "".join(f"<span><b>{html.escape(k)}</b> {html.escape(v)}</span>" for k, v in items)


def render_page(source_bytes: bytes, source_name: str, template: str) -> str:
    source = source_bytes.decode("utf-8")
    h1, body, toc, _ = render_markdown(source)
    if " — " in h1:
        title, sub = h1.split(" — ", 1)
    else:
        title, sub = h1, ""
    toc_html = "\n".join(
        f'<a class="toc-{lvl}" href="#{sid}">{inner}</a>' for lvl, sid, inner in toc
    )
    repl = {
        "@@TITLE@@": html.escape(title),
        "@@SHA@@": hashlib.sha256(source_bytes).hexdigest(),
        "@@SOURCE@@": html.escape(source_name),
        "@@H1@@": html.escape(title),
        "@@SUB@@": html.escape(sub),
        "@@META@@": build_meta(source),
        "@@TOC@@": toc_html,
        "@@BODY@@": body.rstrip("\n") + "\n",
    }
    out = template
    for key in (
        "@@TITLE@@",
        "@@SHA@@",
        "@@SOURCE@@",
        "@@H1@@",
        "@@SUB@@",
        "@@META@@",
        "@@TOC@@",
        "@@BODY@@",
    ):
        out = out.replace(key, repl[key])
    return out


def display_name(md_path: Path) -> str:
    try:
        return md_path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return md_path.name


def generate(md_path: Path) -> str:
    template = TEMPLATE.read_text(encoding="utf-8")
    return render_page(md_path.read_bytes(), display_name(md_path), template)


def self_test() -> int:
    failures: list[str] = []

    def expect(cond: bool, msg: str) -> None:
        if not cond:
            failures.append(msg)

    expect(slugify("1.2 Byte 5 — flags + hop") == "1-2-byte-5-flags-hop", "slug emdash")
    expect(slugify("2.3 :sto and the store") == "2-3-sto-and-the-store", "slug colon")
    sample = (
        "# Title X — Sub text\n\nIntro <b>&</b> `a<b`.\n\n## 1. A\n\n### 1.1 `/X=` key\n\n"
        '## 1. A\n\n| h | v |\n|---|---|\n| `<x>` | 1 |\n\n```\n<pre> & "q"\n```\n'
    )
    h1, body, toc, _ = render_markdown(sample)
    expect(h1 == "Title X — Sub text", "h1 extraction")
    expect([t[1] for t in toc] == ["1-a", "1-1-x-key", "1-a-2"], f"toc ids {toc}")
    expect("&lt;b&gt;&amp;&lt;/b&gt;" in body, "raw html escaped")
    expect('<div class="tbl"><table>' in body, "table wrapper")
    expect("<td><code>&lt;x&gt;</code></td>" in body, "table cell escaped")
    expect("&lt;pre&gt; &amp; &quot;q&quot;" in body, "fence escaped")
    template = TEMPLATE.read_text(encoding="utf-8")
    page1 = render_page(sample.encode(), "x.md", template)
    page2 = render_page(sample.encode(), "x.md", template)
    expect(page1 == page2, "deterministic")
    sha = hashlib.sha256(sample.encode()).hexdigest()
    expect(f'<meta name="source-sha256" content="{sha}">' in page1, "sha meta")
    expect("@@" not in page1, "unreplaced placeholder")
    for msg in failures:
        print(f"self-test FAIL: {msg}", file=sys.stderr)
    if not failures:
        print("self-test ok")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--md", type=Path, default=DEFAULT_MD)
    ap.add_argument("--html", type=Path, default=DEFAULT_HTML)
    ap.add_argument("--check", action="store_true", help="exit 1 if HTML is stale")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    new = generate(args.md)
    if args.check:
        old = args.html.read_text(encoding="utf-8") if args.html.exists() else ""
        if old == new:
            print(f"{args.html.name}: up to date")
            return 0
        diff = list(
            difflib.unified_diff(
                old.splitlines(), new.splitlines(), "committed", "generated", lineterm="", n=0
            )
        )
        changed = sum(1 for d in diff if d[:1] in "+-" and not d.startswith(("+++", "---")))
        print(
            f"{args.html.name}: STALE ({changed} changed lines); "
            f"run: uv run tools/wire_doc_html.py",
            file=sys.stderr,
        )
        for line in diff[:12]:
            print(line[:160], file=sys.stderr)
        return 1

    args.html.write_text(new, encoding="utf-8")
    print(f"wrote {args.html}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
