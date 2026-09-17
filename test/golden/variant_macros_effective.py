#!/usr/bin/env python3
"""Effective-macro gate for the `W7` variants restructure (audit D6-01/02/03).

WHY THIS EXISTS, given that variant_macros_lint.py is right there.

`variant_macros_lint.py` reads each `variants/<env>/configuration.h` AS TEXT
and records what that file literally writes down. That is the correct
instrument for the job it has -- catching a macro that drifted between two
board headers -- and it is useless for `W7`, which MOVES those lines. Hoisting
`RF_FREQUENCY` out of 28 variant headers into one shared default changes 28
entries in that tool's baseline by construction, so afterwards it cannot tell
a correct hoist from one that silently dropped the value for a single board.
Regenerating its baseline to make it pass would throw away the only signal.

So `W7` needs the other half: not "what does this file say" but "what does the
COMPILER end up seeing for this env". That is what this tool records. Between
a before-run and an after-run of the same envs, every line must be identical.
A hoist that is correct produces a byte-identical dump even though every
source file involved has changed; a hoist that is wrong for one board shows up
as one changed line naming the env, the macro, and both values.

HOW THE MACRO SET IS OBTAINED. From PlatformIO's own compilation database
(`pio run -t compiledb -e <env>`), never from a reimplementation of how
PlatformIO assembles flags. The database holds the exact argv the compiler is
invoked with for each translation unit; this tool takes the entry for a fixed
TU, replaces the compile action with `-dM -E`, and reads back the macro set.
Building our own flag model was the obvious shortcut and is the reason this
file says so explicitly: a model of the build drifts from the build, and the
drift is invisible until a board misbehaves in the field.

WHAT IS FILTERED OUT. Compiler and libc built-ins (`__GNUC__`, `__cplusplus`,
`__ARM_*`, ...) swamp the interesting set and change when a toolchain is
upgraded, which has nothing to do with this refactor. Only macros that are
NOT predefined by the bare compiler survive: the tool preprocesses an empty
file with the same compiler and subtracts that set. What remains is what the
project -- `platformio.ini` build flags plus the header chain -- contributed.

USAGE
    python3 test/golden/variant_macros_effective.py --generate   # write baseline
    python3 test/golden/variant_macros_effective.py              # check (exit 1 on drift)
    python3 test/golden/variant_macros_effective.py --env NAME   # one env, print only
    python3 test/golden/variant_macros_effective.py --self-test

The baseline is test/golden/native/variant-macros-effective.txt. It is an
OUTPUT for `--generate` and an EXPECTATION for a check run; regenerating it to
clear a failure is how a real drift gets acknowledged away, so do it only when
the change to it has been read line by line and is understood.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "test" / "golden" / "native" / "variant-macros-effective.txt"

# The TU whose command line is borrowed. Any file compiled by every board env
# would do; this one is picked because it is plain C++ with no board-specific
# guard around its own inclusion, so its entry is present in every database.
PROBE_TU = "src/loop_functions.cpp"

# Macros whose value is a build timestamp or path and therefore differs between
# two runs of the SAME tree. Recording them would make every run "drift".
VOLATILE = re.compile(
    r"^(__DATE__|__TIME__|__TIMESTAMP__|__FILE__|__BASE_FILE__|"
    r"PIO_(SRC|BUILD)_.*|.*_BUILD_(DATE|TIME).*)$"
)


def board_envs(root: Path = ROOT) -> List[str]:
    """Every board env, i.e. every env declared in a `variants/*/platformio.ini`.

    That file is the signal, not `default_envs` and not the root
    `platformio.ini`. Three reasons, each of which cost something to learn:

    * `default_envs` omits live envs. `t5_epaper` sits there commented out and
      `vision-master-e213-preview` was never in it, yet the campaign's own
      sweep builds both -- they are two of the 34. Keying on `default_envs`
      would silently drop exactly the two envs most likely to rot unnoticed.
    * The root `platformio.ini` also declares the `native*` test envs and the
      two safeboot envs. Neither compiles a `variants/<name>/configuration.h`,
      so neither has an effective macro set this gate is about.
    * `vision-master-e213-preview` has no directory of its own; it is declared
      inside `variants/vision-master-e213/platformio.ini`. A glob over
      directories misses it, which is why this reads the ini files.
    """
    envs: List[str] = []
    for sub in sorted((root / "variants").glob("*/platformio.ini")):
        for name in re.findall(r"^\[env:([^\]]+)\]", sub.read_text(), re.M):
            if name not in envs:
                envs.append(name)
    return envs


def compiledb(env: str, root: Path = ROOT) -> List[dict]:
    """PlatformIO's compilation database for one env.

    It lands at the PROJECT ROOT, not under `.pio/build/<env>/`, and each run
    overwrites the previous env's file -- so it is read immediately, per env,
    and the file is left where PlatformIO put it (it is a build artefact and
    must not be staged).
    """
    subprocess.run(
        ["pio", "run", "-t", "compiledb", "-e", env],
        cwd=root, check=True, capture_output=True, text=True,
    )
    out = root / "compile_commands.json"
    if not out.exists():
        raise RuntimeError("pio wrote no compile_commands.json at the project root")
    return json.loads(out.read_text())


def probe_argv(db: List[dict], root: Path = ROOT) -> List[str]:
    """The argv for PROBE_TU, rewritten to dump macros instead of compiling."""
    entry = None
    for e in db:
        f = e.get("file", "")
        if f.endswith(PROBE_TU) or Path(f).name == Path(PROBE_TU).name:
            entry = e
            break
    if entry is None:
        raise RuntimeError(f"{PROBE_TU} not in the compilation database")

    argv = entry.get("arguments") or shlex.split(entry["command"])
    out: List[str] = []
    skip = False
    for a in argv:
        if skip:
            skip = False
            continue
        if a == "-o":            # drop the object-file destination
            skip = True
            continue
        if a in ("-c",):         # not compiling
            continue
        if a.endswith(".cpp") or a.endswith(".c"):   # drop the input file
            continue
        # Dependency generation: the borrowed argv carries -MMD, which makes
        # every probe run drop an `empty.d`/`probe.d` in the working directory.
        # Harmless but untidy, and untracked files in the repo root are exactly
        # what gets staged by accident.
        if a in ("-MMD", "-MD", "-MP"):
            continue
        if a in ("-MF", "-MT", "-MQ"):
            skip = True
            continue
        out.append(a)
    return out


def macros_for(argv: List[str], source: Path) -> Dict[str, str]:
    r = subprocess.run(argv + ["-dM", "-E", str(source)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip()[:400])
    out: Dict[str, str] = {}
    for line in r.stdout.splitlines():
        # `#define NAME body` or `#define NAME(args) body`. The name is a full
        # identifier: an earlier non-greedy `(\S+?)` matched a SINGLE letter
        # here and silently recorded ARDUHAL_LOG_RESET_COLOR as A=RDUHAL_...,
        # which still produced a plausible-looking 1463-line baseline.
        m = re.match(r"#define ([A-Za-z_][A-Za-z0-9_]*)(\([^)]*\))?(?:\s+(.*))?$",
                     line)
        if not m:
            continue
        name = m.group(1)
        if VOLATILE.match(name):
            continue
        args = m.group(2) or ""
        body = m.group(3) or ""
        out[name + args] = body
    return out


def variant_macro_names(root: Path = ROOT) -> set:
    """Every macro name any `variants/*/configuration.h` defines, anywhere.

    This is the set W7 can move, and therefore the only set worth recording.
    Preprocessing `configuration.h` drags in the whole Arduino/ESP-IDF header
    tree -- an unfiltered dump was ~14 000 macros per env and 448 000 lines,
    which is not a baseline anyone will read and which churns wholesale on a
    framework bump. Restricting to these names keeps the gate pointed at the
    refactor instead of at the SDK.
    """
    names = set()
    pat = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)", re.M)
    sources = sorted(root.glob("variants/*/configuration.h"))
    # ...plus src/configuration_global.h, which W7 does NOT move but which
    # W7 can still change the meaning of. Its buffer sizes sit behind
    # `#elif defined(CONFIG_IDF_TARGET_ESP32S3)` / `defined(ENABLE_TBEAM)`
    # style branches, so a hoist that alters which -D flags a board ends up
    # with silently re-picks a branch: MAX_LOG 20 instead of 10, MAX_MHEARD 50
    # instead of 80. That is exactly the failure this gate has to catch, and
    # it is invisible if only the variant headers' own names are recorded.
    sources.append(root / "src" / "configuration_global.h")
    for cfg in sources:
        if not cfg.exists():
            continue
        text = re.sub(r"/\*.*?\*/", "", cfg.read_text(errors="replace"), flags=re.S)
        names.update(pat.findall(text))
    return names


def effective(env: str, root: Path = ROOT,
              names: "set | None" = None) -> Dict[str, str]:
    """What the compiler actually sees, for the macros this refactor can move.

    Three layers, and the middle one is the point:
      * the compiler's own predefines -- subtracted, so a toolchain upgrade
        cancels out on both sides of a before/after comparison;
      * what `platformio.ini` contributes via `-D` -- KEPT, because the
        `extends=` half of W7 moves exactly those;
      * what `variants/<env>/configuration.h` contributes -- KEPT, because the
        `configuration_default.h` half moves exactly those.
    Everything the framework headers drag in transitively is dropped.
    """
    if names is None:
        names = variant_macro_names(root)
    argv = probe_argv(compiledb(env, root), root)
    with tempfile.TemporaryDirectory() as td:
        empty = Path(td) / "empty.cpp"
        empty.write_text("")
        real = Path(td) / "probe.cpp"
        real.write_text('#include "configuration.h"\n')

        bare = [a for a in argv
                if not a.startswith(("-I", "-D", "-include", "-isystem"))]
        builtin = macros_for(bare, empty)      # compiler predefines only
        cmdline = macros_for(argv, empty)      # + the -D flags
        full = macros_for(argv, real)          # + configuration.h's chain

    out: Dict[str, str] = {}
    for k, v in full.items():
        base = k.split("(")[0]
        from_flags = k in cmdline and (k not in builtin or builtin[k] != v)
        from_variant = base in names
        if from_flags or from_variant:
            out[k] = v
    return out


def render(table: Dict[str, Dict[str, str]]) -> str:
    lines = []
    for env in sorted(table):
        for name in sorted(table[env]):
            lines.append(f"{env} {name}={table[env][name]}")
    return "\n".join(lines) + "\n"


def collect(envs: List[str]) -> Tuple[Dict[str, Dict[str, str]], List[str]]:
    table, failed = {}, []
    names = variant_macro_names()
    print(f"macro names any variants/*/configuration.h defines: {len(names)}",
          file=sys.stderr)
    for env in envs:
        try:
            table[env] = effective(env, names=names)
            print(f"  {env}: {len(table[env])} macros", file=sys.stderr)
        except Exception as exc:                       # noqa: BLE001
            failed.append(f"{env}: {exc}")
            print(f"  {env}: FAILED -- {exc}", file=sys.stderr)
    return table, failed


def self_test() -> int:
    """Checks the parts that do not need a toolchain: argv rewriting, the
    built-in subtraction, and the renderer's ordering."""
    ok = True

    def report(name: str, good: bool) -> None:
        nonlocal ok
        print(("PASS  " if good else "FAIL  ") + name)
        ok = ok and good

    db = [{"file": "/x/y/src/loop_functions.cpp",
           "command": "g++ -c -o /x/o.o -DFOO=1 -I/inc /x/y/src/loop_functions.cpp"}]
    argv = probe_argv(db)
    report("argv drops -c, -o and its value, and the input file",
           argv == ["g++", "-DFOO=1", "-I/inc"])

    db2 = [{"file": "/x/y/src/loop_functions.cpp",
            "command": "g++ -c -MMD -MF /x/o.d -o /x/o.o -DA=1 "
                       "/x/y/src/loop_functions.cpp"}]
    report("argv drops dependency generation, so probes leave no .d files",
           probe_argv(db2) == ["g++", "-DA=1"])

    try:
        probe_argv([{"file": "/x/other.cpp", "command": "g++ -c /x/other.cpp"}])
        report("missing probe TU raises", False)
    except RuntimeError:
        report("missing probe TU raises", True)

    class _R:
        returncode = 0
        stdout = ("#define ARDUHAL_LOG_RESET_COLOR \n"
                  "#define MAX(a,b) ((a)>(b)?(a):(b))\n"
                  "#define RF_FREQUENCY 433.175000\n"
                  "#define BARE\n")
        stderr = ""
    _real_run = subprocess.run
    subprocess.run = lambda *a, **k: _R()          # type: ignore[assignment]
    try:
        parsed = macros_for(["cc"], Path("/dev/null"))
    finally:
        subprocess.run = _real_run                 # type: ignore[assignment]
    report("full identifier is the macro name, not its first letter",
           parsed.get("ARDUHAL_LOG_RESET_COLOR") == ""
           and "A" not in parsed)
    report("function-like macros keep their parameter list",
           parsed.get("MAX(a,b)") == "((a)>(b)?(a):(b))")
    report("value macros keep their value",
           parsed.get("RF_FREQUENCY") == "433.175000")
    report("a bodyless #define is recorded as empty, not dropped",
           parsed.get("BARE") == "")

    names = variant_macro_names()
    report("macro-name set covers the variant headers AND the global buffers",
           200 < len(names) < 600
           and {"RF_FREQUENCY", "LORA_SF", "LORA_CR"} <= names
           and {"MAX_LOG", "MAX_MHEARD", "MAX_RING"} <= names)

    report("volatile macros are filtered",
           all(VOLATILE.match(n) for n in ("__DATE__", "__TIME__", "__FILE__"))
           and not VOLATILE.match("RF_FREQUENCY"))

    r = render({"b": {"M": "2"}, "a": {"Z": "1", "A": "0"}})
    report("render sorts by env then macro",
           r == "a A=0\na Z=1\nb M=2\n")

    envs = board_envs()
    report("board env list excludes safeboot and the native suites",
           len(envs) >= 30
           and not any("safeboot" in e for e in envs)
           and not any(e.startswith("native") for e in envs))
    report("board env list keeps the two envs outside default_envs",
           "t5_epaper" in envs and "vision-master-e213-preview" in envs)

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--generate", action="store_true", help="write the baseline")
    ap.add_argument("--env", help="one env; print its macro set and exit")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if args.env:
        for name, val in sorted(effective(args.env).items()):
            print(f"{name}={val}")
        return 0

    envs = board_envs()
    print(f"collecting effective macros for {len(envs)} envs", file=sys.stderr)
    table, failed = collect(envs)
    if failed:
        print("\nENVS THAT DID NOT YIELD A MACRO SET:", file=sys.stderr)
        for f in failed:
            print("  " + f, file=sys.stderr)
        print("A gate that silently skips an env is not a gate.", file=sys.stderr)
        return 1

    text = render(table)
    if args.generate:
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        BASELINE.write_text(text)
        print(f"wrote {BASELINE} ({len(text.splitlines())} lines)")
        return 0

    if not BASELINE.exists():
        print(f"no baseline at {BASELINE}; run --generate first", file=sys.stderr)
        return 1

    want = BASELINE.read_text()
    if want == text:
        print(f"effective macro sets unchanged ({len(text.splitlines())} lines, "
              f"{len(table)} envs)")
        return 0

    import difflib
    diff = list(difflib.unified_diff(want.splitlines(), text.splitlines(),
                                     "baseline", "now", lineterm=""))
    print(f"EFFECTIVE MACRO DRIFT: {len(diff)} diff lines", file=sys.stderr)
    for line in diff[:200]:
        print(line, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
