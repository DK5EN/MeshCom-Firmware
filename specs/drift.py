#!/usr/bin/env python3
"""Drift check between the hand-written TLA+ models and the firmware functions they describe. No solver.

A TLA+ model is evidence only while it matches the C code it was written from. specs/manifest.json lists, per
model, the source functions it describes and a hash of each function body at the last review. This tool
recomputes the hashes. A changed or vanished function means: read the change, update the model if the behaviour
it describes changed, then accept the new hash.

The bounded-model-checking jobs are not listed: they compile the real source, so they cannot drift.

  drift.py --check              exit 0 no drift, 1 drift or a listed function is missing, 2 manifest error
  drift.py --accept <model>     record the current hashes of one model as reviewed (after reading the diff)
  drift.py --accept-all         same for every active model
  drift.py --self-test
"""
from __future__ import annotations

import datetime
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "specs" / "manifest.json"


def function_body(text: str, name: str) -> str | None:
    """Text of the definition of `name`: from its signature line at column 0 to the closing brace at column 0.

    Whole file when name is "*". Declarations (ending in ';' before any '{') are skipped.
    """
    if name == "*":
        return text
    lines = text.splitlines()
    sig = re.compile(rf"^(?:__attribute__\(\([^)]*\)\)\s*)?[A-Za-z_][\w \t\*:<>,&~]*[ \t\*&:]{re.escape(name)}\s*\(")
    for i, line in enumerate(lines):
        if not sig.match(line):
            continue
        j = i
        opened = False
        while j < len(lines):
            if not opened:
                if "{" in lines[j]:
                    opened = True
                elif lines[j].rstrip().endswith(";"):
                    break  # a declaration
            if opened and lines[j].startswith("}"):
                return "\n".join(lines[i : j + 1])
            j += 1
    return None


def normalise(body: str) -> str:
    """Drop // comments, trailing blanks and empty lines: a comment edit is not drift."""
    out: list[str] = []
    for line in body.splitlines():
        line = re.sub(r"//.*$", "", line).rstrip()
        if line.strip():
            out.append(line)
    return "\n".join(out)


def digest(body: str) -> str:
    return hashlib.sha256(normalise(body).encode()).hexdigest()[:16]


def current(model: dict, root: pathlib.Path) -> tuple[dict[str, str], list[str]]:
    got: dict[str, str] = {}
    problems: list[str] = []
    for src in model["sources"]:
        path = root / src["file"]
        if not path.is_file():
            problems.append(f"{src['file']}: file missing")
            continue
        text = path.read_text(errors="replace")
        for fn in src["functions"]:
            body = function_body(text, fn)
            key = f"{src['file']}::{fn}"
            if body is None:
                problems.append(f"{key}: definition not found")
            else:
                got[key] = digest(body)
    return got, problems


def check(manifest: dict, root: pathlib.Path) -> list[str]:
    findings: list[str] = []
    for model in manifest["models"]:
        if model.get("status", "active") != "active":
            continue
        got, problems = current(model, root)
        for p in problems:
            findings.append(f"{model['id']}: {p}")
        want: dict[str, str] = model.get("hashes", {})
        for key, h in got.items():
            if key not in want:
                findings.append(f"{model['id']}: {key} has no reviewed hash (run --accept after review)")
            elif want[key] != h:
                findings.append(f"{model['id']}: {key} changed since the review of {model.get('reviewed', '?')}")
    return findings


def accept(manifest: dict, root: pathlib.Path, which: str | None) -> int:
    n = 0
    for model in manifest["models"]:
        if model.get("status", "active") != "active":
            continue
        if which is not None and model["id"] != which:
            continue
        got, problems = current(model, root)
        if problems:
            for p in problems:
                print(f"{model['id']}: {p}")
            return 2
        model["hashes"] = got
        model["reviewed"] = datetime.date.today().isoformat()
        n += 1
    if n == 0:
        print(f"no active model named {which}")
        return 2
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"accepted {n} model(s)")
    return 0


def self_test() -> int:
    src = (
        "static int helper(int a);\n"
        "// comment\n"
        "static int helper(int a)\n{\n    return a + 1; // inc\n}\n\n"
        "bool Klass::other(const char *p,\n                  int n)\n{\n    if (n) {\n        return true;\n    }\n    return false;\n}\n"
    )
    body = function_body(src, "helper")
    assert body is not None and body.startswith("static int helper(int a)\n{") and body.endswith("}"), body
    assert "return a + 1" in body and "other" not in body
    two = function_body(src, "other")
    assert two is not None and "return false;" in two and two.count("}") == 2, two
    assert function_body(src, "missing") is None
    attr = "__attribute__((noinline)) static void drain(void)\n{\n    work();\n}\n"
    assert function_body(attr, "drain") is not None
    assert function_body(src, "*") == src
    assert digest(body) == digest(body.replace("// inc", "// something else") + "\n\n")
    assert digest(body) != digest(body.replace("a + 1", "a + 2"))
    model = {"id": "m", "sources": [{"file": "f.cpp", "functions": ["helper"]}]}
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        root = pathlib.Path(d)
        (root / "f.cpp").write_text(src)
        got, problems = current(model, root)
        assert not problems and len(got) == 1
        model["hashes"] = got
        assert check({"models": [model]}, root) == []
        (root / "f.cpp").write_text(src.replace("a + 1", "a - 1"))
        assert len(check({"models": [model]}, root)) == 1
        model["sources"][0]["functions"].append("gone")
        assert any("not found" in f for f in check({"models": [model]}, root))
        assert check({"models": [dict(model, status="frozen")]}, root) == []
    print("drift.py self-test: ok")
    return 0


def main() -> int:
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    try:
        manifest = json.loads(MANIFEST.read_text())
    except (OSError, ValueError) as exc:
        print(f"manifest error: {exc}")
        return 2
    if args == ["--check"]:
        findings = check(manifest, ROOT)
        for f in findings:
            print(f"DRIFT {f}")
        active = sum(1 for m in manifest["models"] if m.get("status", "active") == "active")
        print(f"drift.py: {active} active models, {len(findings)} findings")
        if findings:
            print("read the source change, update the model if needed, then: specs/drift.py --accept <model>")
        return 1 if findings else 0
    if args == ["--accept-all"]:
        return accept(manifest, ROOT, None)
    if len(args) == 2 and args[0] == "--accept":
        return accept(manifest, ROOT, args[1])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
