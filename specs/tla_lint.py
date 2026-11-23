#!/usr/bin/env python3
"""Static lint for the TLA+ models and cfgs in specs/tla. No Java, no TLC.

Catches the mistakes a blind edit makes most often:
  - an action that neither primes a variable nor lists it under UNCHANGED (TLC then fails at run time)
  - a cfg that sets a constant the module does not declare, or misses one it declares
  - a cfg that checks or expects a property the module does not define
  - unbalanced brackets in a module
It is not a parser: a clean lint does not replace `run_tlc.sh --sany-all`.
Usage: tla_lint.py [dir]      exit 0 clean, 1 findings
"""
from __future__ import annotations

import pathlib
import re
import sys

KW = {"SPECIFICATION", "CONSTANT", "CONSTANTS", "INVARIANT", "INVARIANTS", "PROPERTY", "PROPERTIES", "INIT", "NEXT"}


def strip_comments(text: str) -> str:
    text = re.sub(r"\(\*.*?\*\)", " ", text, flags=re.S)
    return "\n".join(line.split("\\*")[0] for line in text.splitlines())


def module_info(text: str) -> tuple[list[str], list[str], dict[str, str]]:
    body = strip_comments(text)
    consts: list[str] = []
    for m in re.finditer(r"^CONSTANTS?\b(.*?)(?=^\S)", body, flags=re.S | re.M):
        consts += re.findall(r"[A-Za-z_]\w*", m.group(1))
    varz: list[str] = []
    for m in re.finditer(r"^VARIABLES?\b(.*?)(?=^\S)", body, flags=re.S | re.M):
        varz += re.findall(r"[A-Za-z_]\w*", m.group(1))
    defs: dict[str, str] = {}
    for m in re.finditer(r"^([A-Za-z_]\w*)(\([^)]*\))?\s*==(.*?)(?=^[A-Za-z_]\w*(\([^)]*\))?\s*==|^=====|^RECURSIVE|\Z)", body, flags=re.S | re.M):
        defs[m.group(1)] = m.group(3)
    return consts, varz, defs


def expand(names: set[str], defs: dict[str, str], varz: set[str]) -> set[str]:
    """Variables named by a tuple of identifiers, following tuple-valued definitions (vars == <<...>>)."""
    out: set[str] = set()
    todo = list(names)
    seen: set[str] = set()
    while todo:
        n = todo.pop()
        if n in seen:
            continue
        seen.add(n)
        if n in varz:
            out.add(n)
        elif n in defs and defs[n].strip().startswith("<<"):
            todo += re.findall(r"[A-Za-z_]\w*", defs[n])
    return out


def action_frame(name: str, defs: dict[str, str], varz: set[str], seen: frozenset[str] = frozenset()) -> set[str] | None:
    """Variables an action constrains in the next state; None if the definition is not an action."""
    body = defs[name]
    got: set[str] = {v for v in varz if re.search(rf"(?<![\w']){re.escape(v)}'", body)}
    for m in re.finditer(r"UNCHANGED\s*(<<[^>]*>>|[A-Za-z_]\w*)", body):
        got |= expand(set(re.findall(r"[A-Za-z_]\w*", m.group(1))), defs, varz)
    for other in defs:
        if other != name and other not in seen and re.search(rf"(?<![\w']){re.escape(other)}\b(?!')", body):
            sub = action_frame(other, defs, varz, seen | {name})
            if sub:
                got |= sub
    return got or None


def lint_module(path: pathlib.Path) -> list[str]:
    text = path.read_text()
    out: list[str] = []
    body = strip_comments(text)
    for a, b in (("(", ")"), ("[", "]"), ("{", "}"), ("<<", ">>")):
        if body.count(a) != body.count(b):
            out.append(f"{path.name}: unbalanced {a} {b}: {body.count(a)} vs {body.count(b)}")
    consts, varz, defs = module_info(text)
    vs = set(varz)
    nxt = defs.get("Next", "")
    # every definition reachable from Next that primes something is an action and must frame every variable
    reach: set[str] = set()
    todo = ["Next"]
    while todo:
        n = todo.pop()
        if n in reach or n not in defs:
            continue
        reach.add(n)
        todo += [d for d in defs if re.search(rf"(?<![\w']){re.escape(d)}\b", defs[n])]
    if not nxt:
        out.append(f"{path.name}: no Next")
    for n in sorted(reach - {"Next"}):
        body_n = defs[n]
        if "'" not in body_n and "UNCHANGED" not in body_n:
            continue
        # sub-formulas (helpers used inside a bigger action) are judged through the actions that use them
        used_in = [d for d in reach if d != n and d != "Next" and re.search(rf"(?<![\w']){re.escape(n)}\b", defs[d])
                   and ("'" in defs[d] or "UNCHANGED" in defs[d])]
        if used_in and not re.search(rf"(?<![\w']){re.escape(n)}\b", nxt):
            continue
        frame = action_frame(n, defs, vs) or set()
        missing = sorted(vs - frame)
        if missing and any(not re.search(rf"(?<![\w']){re.escape(n)}\b", defs[d]) for d in reach if d != n):
            if not used_in or re.search(rf"(?<![\w']){re.escape(n)}\b", nxt):
                out.append(f"{path.name}: action {n} leaves unconstrained: {', '.join(missing)}")
    return out


def cfg_tokens(text: str) -> tuple[dict[str, str], list[str], list[str]]:
    consts: dict[str, str] = {}
    inv: list[str] = []
    prop: list[str] = []
    cur = ""
    toks: list[str] = []
    for line in text.splitlines():
        if line.lstrip().startswith("\\*"):
            continue
        toks += re.findall(r"\{[^}]*\}|\"[^\"]*\"|[^\s]+", line)
    i = 0
    while i < len(toks):
        t = toks[i]
        if t in KW:
            cur = t
        elif cur.startswith("CONSTANT"):
            if i + 2 < len(toks) and toks[i + 1] == "=":
                consts[t] = toks[i + 2]
                i += 2
        elif cur.startswith("INVARIANT"):
            inv.append(t)
        elif cur.startswith("PROPERT"):
            prop.append(t)
        i += 1
    return consts, inv, prop


def lint_cfg(path: pathlib.Path) -> list[str]:
    text = path.read_text()
    head = "\n".join(text.splitlines()[:12])
    m = re.search(r"tlc: spec=([\w.]+)", head)
    if not m:
        return [f"{path.name}: no spec marker"]
    spec = path.parent / m.group(1)
    if not spec.is_file():
        return [f"{path.name}: spec {m.group(1)} not found"]
    consts, _varz, defs = module_info(spec.read_text())
    got, inv, prop = cfg_tokens(text)
    out: list[str] = []
    for c in consts:
        if c not in got:
            out.append(f"{path.name}: constant {c} not set")
    for c in got:
        if c not in consts:
            out.append(f"{path.name}: sets unknown constant {c}")
    for p in inv + prop:
        if p not in defs:
            out.append(f"{path.name}: checks undefined {p}")
    if re.search(r"tlc: expect-fail(?!=)", head):
        out.append(f"{path.name}: expect-fail without a property name")
    e = re.search(r"tlc: expect-fail=(\w+)", head)
    if e and e.group(1) not in inv + prop:
        out.append(f"{path.name}: expects a violation of {e.group(1)} but does not check it")
    if e and e.group(1) in prop and len(prop) != 1:
        out.append(f"{path.name}: expected temporal violation needs exactly one PROPERTY (TLC does not name it)")
    return out


def main() -> int:
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).parent / "tla"
    findings: list[str] = []
    mods = sorted(root.glob("*.tla"))
    cfgs = sorted(root.glob("*.cfg"))
    for p in mods:
        findings += lint_module(p)
    for p in cfgs:
        findings += lint_cfg(p)
    for f in findings:
        print(f)
    print(f"tla_lint: {len(mods)} modules, {len(cfgs)} cfgs, {len(findings)} findings")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
