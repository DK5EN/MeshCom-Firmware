#!/usr/bin/env python3
"""Change-based step selection for tools/regression.sh (TESTGATE-01).

    python3 tools/regression_select.py                  # changes of the working tree against HEAD
    python3 tools/regression_select.py --base <commit>  # plus everything committed since <commit>
    python3 tools/regression_select.py --files a b c    # an explicit file list (tests, what-if)
    python3 tools/regression_select.py --shell          # shell assignments for regression.sh
    python3 tools/regression_select.py --self-test

Answers one question: which gate steps can a change to these files turn red?

Rules, in this order:

1. A path under FULL_GATE (platformio.ini, the runner, shared test support that is not
   an include) selects the whole gate.
2. A path under IGNORED (docs, run logs, memory) selects nothing.
3. Unity: an env runs when a changed file is in its include closure. The closure is
   computed from the tree on every call: the suites of the env (test_filter), every file in
   their directories, the src files of build_src_filter, the env's extra_scripts (and the
   tracked files they name, e.g. a force-included stub), and every `#include` reachable from
   those, in both spellings, resolved against the env's -I paths, src/, lib/ and test/support. `#if` is
   ignored, so the closure is a superset of what the compiler reads. A suite that reads
   source files as text (a pin) also depends on every tracked file whose name appears in
   one of its string literals.
4. Every other step has path patterns in STEP_RULES.
5. A changed path that no rule and no closure knows selects the whole gate. The selector
   never guesses "nothing to run" for a file it does not understand.

`--shell` prints MODE (full, select or none), STEPS, UNITY_ENVS, UNITY_GROUPS,
NRF52_LINKS and ESP32_LINKS for `eval`, and the reasons as comment lines. tools/tests/test_regression_select.py
checks the closure against the compiler's own dependency list.
"""

from __future__ import annotations

import argparse
import configparser
import fnmatch
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Step ids as tools/regression.sh knows them.
ALL_STEPS = (
    "unity", "selftest", "links", "ruff", "pytest", "wiredoc", "nbrlog", "mjs", "pages",
    "selftools", "specs", "inventory",
)

# The pytest files that read firmware sources, the build configuration or the build patches.
# A firmware change runs these; a tool change runs the whole pytest step ("pytest").
PYTEST_FIRMWARE = (
    "tools/tests/test_mesh_policy_sites.py", "tools/tests/test_smartbeacon_signed_sites.py",
    "tools/tests/test_au_fences.py", "tools/tests/test_gen_web_assets.py",
    "tools/tests/test_gw_servers_golden.py", "tools/tests/test_tdeck_fonts.py",
    "tools/tests/test_nrf_eth_dns_sites.py", "tools/tests/test_nrf52_gps_probe.py",
    "tools/tests/test_info_netmode_site.py", "tools/tests/test_msgstore_echo_sites.py",
    "tools/tests/test_rm_rx_gate_lint.py", "tools/tests/test_web_escape_lint.py",
    "tools/tests/test_ina_web_site.py", "tools/tests/test_webgui_nav.py",
    "tools/tests/test_gw_net_matrix.py", "tools/tests/test_nimble_patch.py",
    "tools/tests/test_w5100s_patch.py", "tools/tests/test_arduino_esp32_patch.py",
    "tools/tests/test_lwip_wrap.py", "tools/tests/test_pages_flasher.py",
    "tools/tests/test_make_autoupdate.py", "tools/tests/test_remote_cmd.py",
)

FULL_GATE = (
    "platformio.ini", "variants/*/platformio.ini", "tools/regression.sh", "tools/regression_*.py",
    "test/golden/selftest.sh", "ruff.toml",
)

IGNORED = (
    "docs/*", "*.md", ".claude/*", ".github/ISSUE_TEMPLATE/*", "tools/bench/runs/*", "board-requests/*",
    ".gitignore", ".prettierrc", ".prettierignore", "LICENSE*", "release-notes.md",
)

# (pattern, steps). Every pattern that matches adds its steps. "pytest_fw" is the firmware
# subset of the pytest step; "pytest" (the whole step) wins when both are selected.
STEP_RULES: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("src/*", ("selftest", "links", "pytest_fw", "specs")),
    ("variants/*", ("selftest", "links", "pytest_fw")),
    ("lib/*", ("links",)),
    ("src/web_functions/*", ("pages", "mjs")),
    ("src/safeboot/*", ("pages",)),
    ("src/rm_commands.h", ("pages",)),
    ("src/meshcom_settings.h", ("pages",)),
    ("src/remote_cmd.h", ("pages",)),
    ("src/rm_sender_policy.h", ("pages",)),
    ("test/*", ("selftest", "inventory")),
    ("tools/suite_map.py", ("inventory",)),
    ("test/test_au_ui_state/*", ("pages",)),
    ("test/test_nbrlog/*", ("nbrlog",)),
    ("test/test_aprs_corpus/corpus.txt", ("pytest",)),
    ("test/golden/gw_servers_legacy.json", ("pytest_fw",)),
    ("test/*.py", ("ruff",)),
    ("tools/*.py", ("ruff", "pytest", "selftools")),
    ("tools/*.sh", ("pytest",)),
    ("tools/*.cpp", ("pytest_fw",)),
    ("tools/bench/*", ("pytest", "selftest", "selftools")),
    ("tools/mock/*", ("pytest",)),
    ("tools/gps/*", ("pytest",)),
    ("tools/tests/*", ("pytest", "mjs")),
    ("tools/testdata/*", ("pytest", "nbrlog")),
    ("tools/logreport/*", ("pytest",)),
    ("tools/nbr*.py", ("nbrlog",)),
    ("tools/soakstatus.py", ("nbrlog",)),
    ("tools/meshlogger.py", ("nbrlog",)),
    ("tools/nm_symsum.py", ("nbrlog",)),
    ("tools/*.js", ("pages",)),
    ("tools/*.mjs", ("mjs",)),
    ("tools/gen_web_assets.py", ("pages",)),
    ("tools/wire_doc_html.py", ("wiredoc",)),
    ("pages/*", ("pytest", "mjs")),
    (".github/workflows/*", ("pytest",)),
    ("specs/*", ("specs",)),
)

# Files under an IGNORED pattern that a gate step reads all the same. Checked before IGNORED.
READ_BY_A_STEP: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("docs/architecture/11-wire-format.*", ("wiredoc",)),
    ("docs/testplan/drift-matrix.csv", ("selftest",)),
    ("docs/d1-04-*", ("selftest",)),
    ("docs/safeboot-ota-contract.md", ("pages",)),
    ("docs/test-suite-inventory.md", ("inventory",)),
)

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*["<]([^">]+)[">]', re.M)
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
# The repo_* names are the shared readers of test/support/repo_file.h (TESTGATE-01 step 3.3).
# A suite that reads only through them has no fopen of its own; without these names its
# text pins drop out of the closure and a change to the pinned file selects nothing.
FILE_READ_RE = re.compile(
    r"\b(ifstream|fopen|freopen|slurp|read_repo_file|read_file|readFile|load_file"
    r"|repo_slurp|repo_try_slurp|repo_fopen|repo_path|repo_root)\b"
)
NAME_RE = re.compile(r"[\w.+-]+\.(?:cpp|c|h|hpp|ini|html|js|css|json|txt|csv|py|bin|md)\b")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp", ".ino", ".inc")


def git_files(root: Path) -> list[str]:
    """Tracked plus untracked-but-not-ignored files, repo-relative."""
    out = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=root, capture_output=True, text=True, check=True,
    ).stdout
    return [line for line in out.splitlines() if line]


def changed_files(root: Path, base: str | None) -> list[str]:
    """Files that differ from HEAD (staged, unstaged, untracked), plus base..HEAD when given."""
    names: set[str] = set()
    status = subprocess.run(
        ["git", "status", "--porcelain", "--untracked-files=all", "-z"],
        cwd=root, capture_output=True, text=True, check=True,
    ).stdout
    entries = status.split("\0")
    i = 0
    while i < len(entries):
        entry = entries[i]
        i += 1
        if len(entry) < 4:
            continue
        code, path = entry[:2], entry[3:]
        names.add(path)
        if code[0] in "RC":  # the old name of a rename follows as its own entry
            if i < len(entries) and entries[i]:
                names.add(entries[i])
            i += 1
    if base:
        diff = subprocess.run(
            ["git", "diff", "--name-only", "-z", f"{base}...HEAD"],
            cwd=root, capture_output=True, text=True, check=True,
        ).stdout
        names.update(n for n in diff.split("\0") if n)
    return sorted(names)


def matches(path: str, patterns: tuple[str, ...] | list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pat) for pat in patterns)


class Project:
    """The native envs of platformio.ini and their include closures."""

    def __init__(self, root: Path = ROOT, files: list[str] | None = None) -> None:
        self.root = root
        self.files = files if files is not None else git_files(root)
        self.fileset = set(self.files)
        self.by_name: dict[str, list[str]] = {}
        for f in self.files:
            self.by_name.setdefault(f.rsplit("/", 1)[-1], []).append(f)
        cp = configparser.ConfigParser(interpolation=None, strict=False)
        cp.read(root / "platformio.ini", encoding="utf-8")
        self.cp = cp
        self.envs = [s[4:] for s in cp.sections() if s.startswith("env:native")]
        self.suites = sorted(
            {f.split("/")[1] for f in self.files if f.startswith("test/test_") and f.count("/") >= 2}
        )
        self.lib_dirs = sorted({"/".join(f.split("/")[:2]) for f in self.files if f.startswith("lib/")})
        self._text: dict[str, str] = {}
        self._closure: dict[tuple[str, str], frozenset[str]] = {}

    # -- configuration ------------------------------------------------------------------
    def opt(self, env: str, key: str) -> str:
        """An option of the env, following `extends` to its base sections."""
        section = f"env:{env}"
        seen: set[str] = set()
        while section and section not in seen and self.cp.has_section(section):
            seen.add(section)
            if self.cp.has_option(section, key):
                return self.cp.get(section, key)
            section = self.cp.get(section, "extends", fallback="").split(",")[0].strip()
        return ""

    def env_suites(self, env: str) -> list[str]:
        pats = self.opt(env, "test_filter").split()
        ignore = self.opt(env, "test_ignore").split()
        if not pats:
            pats = ["*"]
        return [
            s for s in self.suites
            if matches(s, pats) and not matches(s, ignore)
        ]

    def env_include_dirs(self, env: str) -> list[str]:
        dirs = re.findall(r"-I\s*(\S+)", self.opt(env, "build_flags"))
        return [d.rstrip("/") for d in dirs]

    def env_src_files(self, env: str) -> list[str]:
        if self.opt(env, "test_build_src").strip().lower() not in ("yes", "true", "1"):
            return []
        chosen: set[str] = set()
        filt = self.opt(env, "build_src_filter") or "+<*>"
        for sign, pat in re.findall(r"([+-])<([^>]*)>", filt):
            pat = pat.strip()
            hit = {
                f for f in self.files
                if f.startswith("src/") and f.endswith(SOURCE_SUFFIXES)
                and (fnmatch.fnmatchcase(f[4:], pat) or f[4:].startswith(pat.rstrip("*").rstrip("/") + "/")
                     or f[4:] == pat)
            }
            if sign == "+":
                chosen |= {f for f in hit if f.endswith((".c", ".cc", ".cpp"))}
            else:
                chosen -= hit
        return sorted(chosen)

    # -- closure ------------------------------------------------------------------------
    def text(self, path: str) -> str:
        if path not in self._text:
            try:
                self._text[path] = (self.root / path).read_text(encoding="utf-8", errors="replace")
            except OSError:
                self._text[path] = ""
        return self._text[path]

    def resolve(self, inc: str, from_file: str, search: list[str]) -> list[str]:
        """Every tracked file an include could mean: next to the includer, then the search dirs."""
        here = from_file.rsplit("/", 1)[0] if "/" in from_file else ""
        found = []
        for base in [here, *search]:
            parts: list[str] = []
            for seg in (f"{base}/{inc}" if base else inc).split("/"):
                if seg == "..":
                    if parts:
                        parts.pop()
                elif seg not in (".", ""):
                    parts.append(seg)
            cand = "/".join(parts)
            if cand in self.fileset and cand not in found:
                found.append(cand)
        return found

    def suite_closure(self, env: str, suite: str) -> frozenset[str]:
        key = (env, suite)
        if key in self._closure:
            return self._closure[key]
        suite_dir = f"test/{suite}/"
        suite_files = [f for f in self.files if f.startswith(suite_dir)]
        search = [
            *self.env_include_dirs(env), f"test/{suite}", "src", "test/support", "test", "include",
            *self.lib_dirs, *[f"{d}/src" for d in self.lib_dirs],
        ]
        todo = [f for f in suite_files if f.endswith(SOURCE_SUFFIXES)] + self.env_src_files(env)
        deps: set[str] = set(suite_files) | set(todo)
        for script in re.findall(r"(?:pre:|post:)?(\S+\.py)", self.opt(env, "extra_scripts")):
            if script in self.fileset:
                deps.add(script)
                for literal in STRING_RE.findall(self.text(script)):
                    for name in NAME_RE.findall(literal):
                        for hit in self.by_name.get(name, ()):
                            deps.add(hit)
                            todo.append(hit)
        seen: set[str] = set()
        while todo:
            f = todo.pop()
            if f in seen:
                continue
            seen.add(f)
            for inc in INCLUDE_RE.findall(self.text(f)):
                for hit in self.resolve(inc, f, search):
                    deps.add(hit)
                    todo.append(hit)
                    if hit.startswith("lib/"):  # pio links the whole library of an included header
                        lib = "/".join(hit.split("/")[:2]) + "/"
                        for other in self.files:
                            if other.startswith(lib) and other not in deps:
                                deps.add(other)
                                if other.endswith(SOURCE_SUFFIXES):
                                    todo.append(other)
        # Pins: a suite that reads files as text depends on every tracked file it names.
        for f in suite_files:
            if not f.endswith(SOURCE_SUFFIXES):
                continue
            body = self.text(f)
            if not FILE_READ_RE.search(body):
                continue
            for literal in STRING_RE.findall(body):
                for name in NAME_RE.findall(literal):
                    for hit in self.by_name.get(name, ()):
                        if not hit.startswith("test/test_") or hit.startswith(suite_dir):
                            deps.add(hit)
        result = frozenset(deps)
        self._closure[key] = result
        return result

    def unity_for(self, changed: list[str]) -> tuple[dict[str, set[str]], set[str]]:
        """(env -> suites to run, changed files that some closure covers)."""
        want: dict[str, set[str]] = {}
        covered: set[str] = set()
        ch = set(changed)
        for env in self.envs:
            for suite in self.env_suites(env):
                hit = self.suite_closure(env, suite) & ch
                if hit:
                    want.setdefault(env, set()).add(suite)
                    covered |= hit
        return want, covered

    def nrf52_excluded_dirs(self) -> list[str]:
        """The src/ directories the nRF52 builds leave out (first `+<*>` build_src_filter)."""
        for section in self.cp.sections():
            filt = self.cp.get(section, "build_src_filter", fallback="")
            if "+<*>" in filt and not section.startswith("env:native"):
                return [d for d in re.findall(r"-<([\w.-]+)/\*>", filt)]
        return []


def select(changed: list[str], project: Project) -> dict:
    """The selection for a list of changed repo-relative paths."""
    reasons: list[str] = []
    steps: set[str] = set()
    full = False
    want, covered = project.unity_for(changed)
    nrf_excluded = project.nrf52_excluded_dirs()
    links_needed = False
    esp_links_needed = False
    for path in changed:
        if matches(path, FULL_GATE):
            full = True
            reasons.append(f"{path}: full gate (build configuration or the runner itself)")
            continue
        hit_steps: set[str] = set()
        for pat, st in READ_BY_A_STEP:
            if fnmatch.fnmatchcase(path, pat):
                hit_steps.update(st)
        in_unity = path in covered
        if not hit_steps and not in_unity and matches(path, IGNORED):
            reasons.append(f"{path}: no gate step reads it")
            continue
        if path.startswith("test/support/") and not in_unity:
            full = True
            reasons.append(f"{path}: full gate (shared test support outside every include closure)")
            continue
        for pat, st in STEP_RULES:
            if fnmatch.fnmatchcase(path, pat):
                hit_steps.update(st)
        if "links" in hit_steps:
            sub = path.split("/")[1] if path.startswith("src/") and path.count("/") >= 2 else ""
            if not (sub and sub in nrf_excluded):
                links_needed = True  # part of the nRF52 images
            if sub != "nrf52":
                esp_links_needed = True  # src/nrf52/ is the only directory no ESP32 image builds
        if not hit_steps and not in_unity:
            full = True
            reasons.append(f"{path}: full gate (no rule knows this path)")
            continue
        steps |= hit_steps
        envs = sorted(e for e, ss in want.items() if any(path in project.suite_closure(e, s) for s in ss))
        what = sorted(hit_steps) + ([f"unity: {len(envs)} env(s)"] if envs else [])
        reasons.append(f"{path}: {', '.join(what) if what else 'nothing'}")
    if want:
        steps.add("unity")
    if "pytest" in steps:
        steps.discard("pytest_fw")
    mode = "full" if full else ("select" if steps else "none")
    return {
        "mode": mode,
        "steps": sorted(steps),
        "unity": {e: sorted(s) for e, s in sorted(want.items())},
        "unity_groups": unity_groups(
            {e: sorted(s) for e, s in sorted(want.items())}, {e: project.env_suites(e) for e in want}
        ),
        "links": links_needed,
        "links_esp32": esp_links_needed,
        "reasons": reasons,
        "changed": changed,
    }


def unity_groups(unity: dict[str, list[str]], all_suites: dict[str, list[str]]) -> list[tuple[list[str], list[str]]]:
    """The `pio test` calls of a selection, as (envs, suite filter) pairs.

    A `-f` on the command line replaces the test_filter of EVERY env of that call, so
    one call for several envs with the union of their suites runs each suite in each
    env (found 2026-11-24: three suites in five envs, ten false failures). Envs whose
    selection is all of their suites share one call without a filter; an env with a
    partial selection gets a call of its own.
    """
    whole = [e for e, ss in unity.items() if sorted(ss) == sorted(all_suites[e])]
    groups: list[tuple[list[str], list[str]]] = [(whole, [])] if whole else []
    groups += [([e], sorted(ss)) for e, ss in unity.items() if e not in whole]
    return groups


def shell_lines(sel: dict) -> list[str]:
    envs = " ".join(sel["unity"])
    groups = " ".join(
        ",".join(es) + (":" + ",".join(ss) if ss else "") for es, ss in sel["unity_groups"]
    )
    lines = [f"# {r}" for r in sel["reasons"]]
    lines += [
        f"MODE={sel['mode']}",
        f"STEPS=\"{' '.join(sel['steps'])}\"",
        f'UNITY_ENVS="{envs}"',
        f'UNITY_GROUPS="{groups}"',
        f"NRF52_LINKS={1 if sel['links'] else 0}",
        f"ESP32_LINKS={1 if sel['links_esp32'] else 0}",
        f"PYTEST_FIRMWARE=\"{' '.join(PYTEST_FIRMWARE)}\"",
        f"CHANGED_COUNT={len(sel['changed'])}",
    ]
    return lines


def self_test() -> int:
    """Rules on the real tree: known files must select known steps, unknown ones the full gate."""
    project = Project()
    checks: list[tuple[bool, str]] = []

    def sel(*paths: str) -> dict:
        return select(list(paths), project)

    s = sel("src/rtc_offset.h")
    checks.append(("native_rtc_offset" in s["unity"], "a header selects the env of its suite"))
    checks.append((len(s["unity"]) < len(project.envs) // 2, "a leaf header does not select most envs"))
    checks.append(({"selftest", "links", "pytest_fw", "unity"} <= set(s["steps"]), "a src change runs lints, links, pins"))
    checks.append(("pytest" not in s["steps"], "a src change does not run the tool tests"))
    s = sel("src/dm_dedup.cpp")
    checks.append(("native" in s["unity"] and "test_dm_dedup" in s["unity"]["native"], "a compiled src file selects its env"))
    s = sel("src/esp32/esp32_main.cpp")
    checks.append((s["mode"] == "select" and not s["links"], "an ESP32-only file needs no nRF52 link"))
    checks.append((s["links_esp32"] and "links" in s["steps"], "an ESP32-only file needs the ESP32 builds"))
    checks.append(("native_display_clock" in s["unity"] or "native" in s["unity"], "a pinned file selects the pinning suites"))
    s = sel("src/nrf52/nrf52_main.cpp")
    checks.append((s["links"] and not s["links_esp32"], "an nRF52 file needs the nRF52 links only"))
    s = sel("test/test_rtc_offset/test_rtc_offset.cpp")
    checks.append((list(s["unity"]) == ["native_rtc_offset"], "a test file selects exactly its env"))
    s = sel("tools/bench/meshlogger_helper.py")
    checks.append(("pytest" in s["steps"] and "unity" not in s["steps"], "a bench tool runs pytest, not Unity"))
    s = sel("tools/bench/runs/harness/x/firmware.bin")
    checks.append((s["mode"] == "none", "a bench run artefact selects nothing"))
    s = sel("test/support/nrf52/WisBlock-API.h")
    checks.append((s["mode"] == "select" and len(s["unity"]) > 5, "a shared stub header selects the envs that include it"))
    s = sel("docs/RESUME.md")
    checks.append((s["mode"] == "none", "a plain document selects nothing"))
    s = sel("docs/architecture/11-wire-format.md")
    checks.append((s["steps"] == ["wiredoc"], "a document a step reads selects that step"))
    for path in ("platformio.ini", "tools/regression.sh", "test/support/some_trace.bin", "some/new/dir/file.xyz"):
        checks.append((sel(path)["mode"] == "full", f"{path} selects the full gate"))
    s = sel("docs/RESUME.md", "platformio.ini")
    checks.append((s["mode"] == "full", "one full-gate path makes the whole run full"))
    s = sel("src/web_functions/web/mc.js")
    checks.append(({"pages", "mjs"} <= set(s["steps"]), "a web asset runs the page tests"))
    every = {s for _, st in (*STEP_RULES, *READ_BY_A_STEP) for s in st} | {"unity"}
    checks.append((every - {"pytest_fw"} <= set(ALL_STEPS), "every rule names a known step"))
    for ok, what in checks:
        print(("PASS  " if ok else "FAIL  ") + what)
    return 0 if all(ok for ok, _ in checks) else 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--base", help="also count everything committed since this commit")
    ap.add_argument("--files", nargs="*", help="use this file list instead of git")
    ap.add_argument("--shell", action="store_true", help="print shell assignments")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    project = Project()
    changed = args.files if args.files is not None else changed_files(ROOT, args.base)
    sel = select(changed, project)
    if args.shell:
        print("\n".join(shell_lines(sel)))
        return 0
    for r in sel["reasons"]:
        print(r)
    print(f"mode: {sel['mode']}")
    if sel["mode"] == "select":
        print(f"steps: {' '.join(sel['steps'])}")
        for env, suites in sel["unity"].items():
            print(f"  {env}: {' '.join(suites)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
