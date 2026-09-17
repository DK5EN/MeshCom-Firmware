#!/usr/bin/env python3
"""Effective-config gate for the `W7-II(b)` platformio.ini restructure
(BACKLOG §3.8aq, second half).

WHY THIS EXISTS, given that `variant_macros_effective.py` sits right next to
it and is built for the exact same kind of refactor.

That tool records what the COMPILER sees for the `configuration.h` macro set.
`W7-II(b)` never touches a header -- it moves `.ini` KEYS (`upload_command`,
`monitor_speed`, `upload_protocol`, ...) from 23 `variants/*/platformio.ini`
files into two new base sections (`[esp32_s3]`, `[esp32_classic]`) that they
`extends =` instead. A macro dump cannot see that at all: none of the moved
keys reach the preprocessor as a `-D`. What has to stay byte-identical here is
PlatformIO's own resolved configuration -- the same thing `pio project config
--json-output` prints -- for every one of the 68 envs (board envs, native_*
test envs, both safeboot envs), before and after the hoist.

HOW THE CONFIG IS OBTAINED. From `pio project config --json-output` itself,
never from a reimplementation of PlatformIO's `extends=`/interpolation rules.
That command returns `ProjectConfig.as_tuple()`: a list of
`[section, [[key, value], ...]]` pairs, already `extends`-merged and
`${...}`-interpolated by PlatformIO's own `ProjectConfig.get()`. Re-deriving
that here would risk the exact class of bug this gate exists to catch: a model
of `extends` that quietly disagrees with PlatformIO's.

WHAT IS NORMALISED, and why each one is safe to normalise:

* `${platformio.build_dir}` resolves to an ABSOLUTE path
  (`/Users/.../MeshCom-Firmware-DEV-Main/.pio/build/<env>`). Both the
  committed baseline and every live dump replace that path prefix with the
  literal token `<PROJECT_DIR>` before comparing, so the gate does not fail
  merely because it runs from a different checkout.
* `extends` is IGNORED entirely. It is structural -- it names which section a
  key was inherited from -- not part of what the compiler/uploader sees, and
  W7-II(b) deliberately changes it (`esp32` -> `esp32_s3`/`esp32_classic`).
  Every other key must still match exactly.
* A multi-line value (`build_flags`, `lib_deps`, `lib_ignore`, ...) is
  compared after `.strip()`-ing each line and dropping blank lines, but LINE
  ORDER is kept. `build_flags` order matters (e.g. a `-D` that shadows an
  earlier one), so this is not a set comparison.

USAGE
    python3 test/golden/variant_ini_effective.py --generate    # write baseline
    python3 test/golden/variant_ini_effective.py               # check (exit 1 on drift)
    python3 test/golden/variant_ini_effective.py --self-test

The baseline is test/golden/native/variant-ini-effective.json. It is an
OUTPUT for `--generate` and an EXPECTATION for a check run; regenerating it to
clear a failure is how a real drift gets acknowledged away, so do it only
after the diff has been read line by line and is understood.

NOTE ON WHO RUNS THIS. `pio project config --json-output` shells out to
PlatformIO, which touches `~/.platformio/packages` and can race a concurrent
`pio` process from another wave agent (see MEMORY.md: "PlatformIO package
store is global"). The default (check) and `--generate` modes therefore
require `pio` on PATH and are meant to be run by whoever currently owns the
`pio` slot for the wave, never in parallel with another `pio run`.
`--self-test` needs no toolchain at all: it feeds synthetic dicts through the
normaliser/comparator only.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, List

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "test" / "golden" / "native" / "variant-ini-effective.json"
PROJECT_DIR_TOKEN = "<PROJECT_DIR>"


def normalize_path(text: str, project_dir: str) -> str:
    """Replaces the absolute project path with a stable placeholder.

    ${platformio.build_dir} resolves to `<project_dir>/.pio/build/<env>`, so
    every `upload_command` embeds the absolute checkout path. Without this,
    the gate would fail on every checkout whose path differs from the one
    that captured the baseline -- a false positive that has nothing to do
    with the refactor.
    """
    return text.replace(project_dir, PROJECT_DIR_TOKEN)


def expand_scons_vars(text: str, env_name: str) -> str:
    """Expands the SCons build variables an `upload_command` may use to the
    text PlatformIO's own interpolation would have produced, so a template in
    a base section (`$BUILD_DIR/firmware.bin`, W7-II(b)) compares equal to the
    per-variant `${platformio.build_dir}/${this.__env__}/firmware.bin` line it
    replaced. The upload step substitutes exactly these:
    `$BUILD_DIR` = `$PROJECT_BUILD_DIR/$PIOENV`, `$PIOENV` = the env name.
    `${this.__env__}` cannot be used in a base section -- `pio project config`
    evaluates the base section on its own and aborts.
    """
    build_dir = PROJECT_DIR_TOKEN + "/.pio/build"
    return (text.replace("$BUILD_DIR", build_dir + "/" + env_name)
                .replace("$PROJECT_BUILD_DIR", build_dir)
                .replace("$PIOENV", env_name))


def normalize_lines(value, project_dir: str, env_name: str = "") -> List[str]:
    """Turns one option's value into the list of stripped, non-blank lines
    that the comparison actually checks, in ORDER.

    `value` is either a single string (a scalar option) or a list of strings
    (a `multiple=True` option such as `build_flags`/`lib_deps`/`lib_ignore`,
    already split into physical lines by PlatformIO). Either way, the path
    placeholder is applied first, then each physical line is split again on
    embedded newlines (defensive: `ProjectConfig.set()` prepends "\\n" to any
    multi-line scalar it writes) and stripped; blank lines are dropped but
    surviving lines keep their original relative order, because
    `build_flags` order is not incidental (a later `-D` can shadow an
    earlier one).
    """
    raw_items = value if isinstance(value, list) else [value]
    out: List[str] = []
    for item in raw_items:
        text = normalize_path(str(item), project_dir)
        if env_name:
            text = expand_scons_vars(text, env_name)
        for line in text.splitlines():
            line = line.strip()
            if line:
                out.append(line)
    return out


def normalize_config(data: Dict[str, dict], project_dir: str) -> Dict[str, Dict[str, List[str]]]:
    """Filters to `env:*` sections, drops the structural `extends` key, and
    normalizes every remaining value to an ordered list of lines.

    `data` is the raw `{section: {key: value}}` mapping -- either the parsed
    baseline JSON or a live `pio project config --json-output` dump reshaped
    the same way by `reshape_pio_json()`.
    """
    out: Dict[str, Dict[str, List[str]]] = {}
    for section, options in data.items():
        if not section.startswith("env:"):
            continue
        opts: Dict[str, List[str]] = {}
        for key, value in options.items():
            if key == "extends":
                continue
            opts[key] = normalize_lines(value, project_dir, section[len("env:"):])
        out[section] = opts
    return out


def reshape_pio_json(raw: list) -> Dict[str, dict]:
    """`pio project config --json-output` prints `[[section, [[k, v], ...]],
    ...]` (ProjectConfig.as_tuple(), JSON-encoded). Reshapes that into the
    same `{section: {key: value}}` mapping the baseline file already uses,
    so both sides go through one normalizer.
    """
    return {section: dict(options) for section, options in raw}


def live_config(root: Path = ROOT) -> Dict[str, dict]:
    """Runs `pio project config --json-output` and reshapes the result.

    Requires `pio` on PATH and touches `~/.platformio/packages`; see the
    module docstring's note on not running this alongside another `pio`
    process.
    """
    r = subprocess.run(
        ["pio", "project", "config", "--json-output"],
        cwd=root, capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError(
            f"pio project config --json-output failed (exit {r.returncode}): "
            f"{r.stderr.strip()[:2000]}"
        )
    return reshape_pio_json(json.loads(r.stdout))


def compare(baseline: Dict[str, Dict[str, List[str]]],
            live: Dict[str, Dict[str, List[str]]]) -> List[str]:
    """Returns a list of human-readable diff lines; empty means a clean match.

    Every env in either side is checked, so an env that disappeared or
    appeared is reported too, not just changed keys within envs that exist
    on both sides.
    """
    diffs: List[str] = []
    envs = sorted(set(baseline) | set(live))
    for env in envs:
        if env not in baseline:
            diffs.append(f"{env}: present in live config but not in baseline")
            continue
        if env not in live:
            diffs.append(f"{env}: present in baseline but missing from live config")
            continue
        b_opts, l_opts = baseline[env], live[env]
        keys = sorted(set(b_opts) | set(l_opts))
        for key in keys:
            b_val = b_opts.get(key)
            l_val = l_opts.get(key)
            if b_val is None:
                diffs.append(f"{env} {key}: added (not in baseline); live={l_val!r}")
            elif l_val is None:
                diffs.append(f"{env} {key}: removed (in baseline, absent live); baseline={b_val!r}")
            elif b_val != l_val:
                diffs.append(
                    f"{env} {key}: expected={b_val!r} actual={l_val!r}"
                )
    return diffs


def count_keys(config: Dict[str, Dict[str, List[str]]]) -> int:
    return sum(len(opts) for opts in config.values())


def self_test() -> int:
    """Checks the normalizer/comparator without needing `pio` or a toolchain."""
    ok = True

    def report(name: str, good: bool) -> None:
        nonlocal ok
        print(("PASS  " if good else "FAIL  ") + name)
        ok = ok and good

    proj = "/Users/x/proj"

    # identical -> pass
    a = {"env:foo": {"board": "b", "build_flags": ["-DA=1", "-DB=2"]}}
    b = {"env:foo": {"board": "b", "build_flags": ["-DA=1", "-DB=2"]}}
    na = normalize_config(a, proj)
    nb = normalize_config(b, proj)
    report("identical configs compare equal", compare(na, nb) == [])

    # extends differs -> still pass (structural key, ignored)
    a2 = {"env:foo": {"board": "b", "extends": ["esp32"]}}
    b2 = {"env:foo": {"board": "b", "extends": ["esp32_s3"]}}
    report("extends is ignored when it legitimately changes",
           compare(normalize_config(a2, proj), normalize_config(b2, proj)) == [])

    # build_flags line reordered -> FAIL (order matters)
    a3 = {"env:foo": {"build_flags": ["-DA=1", "-DB=2"]}}
    b3 = {"env:foo": {"build_flags": ["-DB=2", "-DA=1"]}}
    d3 = compare(normalize_config(a3, proj), normalize_config(b3, proj))
    report("a reordered build_flags line is reported as drift", d3 != [])

    # a value changed -> FAIL
    a4 = {"env:foo": {"board": "old"}}
    b4 = {"env:foo": {"board": "new"}}
    d4 = compare(normalize_config(a4, proj), normalize_config(b4, proj))
    report("a changed scalar value is reported as drift",
           len(d4) == 1 and "old" in d4[0] and "new" in d4[0])

    # path placeholder round-trip
    upload = {
        "env:foo": {
            "upload_command": f'esptool --port "$UPLOAD_PORT" {proj}/.pio/build/foo/bootloader.bin'
        }
    }
    normed = normalize_config(upload, proj)
    report("the absolute project path is replaced by the placeholder",
           any(PROJECT_DIR_TOKEN in line for line in normed["env:foo"]["upload_command"])
           and not any(proj in line for line in normed["env:foo"]["upload_command"]))

    # W7-II(b): a base-section template written with SCons variables must
    # compare equal to the per-variant line it replaced
    old_line = {"env:foo": {"upload_command": f'esptool 0x0 {proj}/.pio/build/foo/bootloader.bin 0xC0000 {proj}/.pio/build/foo/firmware.bin'}}
    new_line = {"env:foo": {"upload_command": 'esptool 0x0 $BUILD_DIR/bootloader.bin 0xC0000 $PROJECT_BUILD_DIR/$PIOENV/firmware.bin'}}
    report("$BUILD_DIR/$PROJECT_BUILD_DIR/$PIOENV expand to the interpolated text",
           compare(normalize_config(old_line, proj), normalize_config(new_line, proj)) == [])
    wrong = {"env:foo": {"upload_command": 'esptool 0x0 $BUILD_DIR/bootloader.bin 0xC0000 $PROJECT_BUILD_DIR/bar/firmware.bin'}}
    report("a template naming another env's build dir is drift",
           compare(normalize_config(old_line, proj), normalize_config(wrong, proj)) != [])

    # a live dump using a DIFFERENT checkout path than the baseline still
    # compares equal once both sides are normalized against their own path.
    baseline_raw = {"env:foo": {"upload_command": f"{proj}/.pio/build/foo/x.bin"}}
    live_raw = {"env:foo": {"upload_command": "/home/other/proj2/.pio/build/foo/x.bin"}}
    b_norm = normalize_config(baseline_raw, proj)
    l_norm = normalize_config(live_raw, "/home/other/proj2")
    report("baseline and live paths each normalize to the same placeholder",
           compare(b_norm, l_norm) == [])

    # blank lines dropped, non-blank lines keep order, across an embedded
    # newline inside what would otherwise look like one scalar item.
    multi = {"env:foo": {"lib_ignore": ["A", "", "  B  ", "C\nD"]}}
    lines = normalize_lines(multi["env:foo"]["lib_ignore"], proj)
    report("blank lines dropped, order kept, embedded newlines split",
           lines == ["A", "B", "C", "D"])

    # reshape_pio_json turns the [[section, [[k, v], ...]], ...] shape from
    # `pio project config --json-output` into the {section: {key: value}}
    # mapping the rest of this tool works with.
    raw = [["env:foo", [["board", "b"], ["build_flags", ["-DA=1"]]]]]
    reshaped = reshape_pio_json(raw)
    report("reshape_pio_json mirrors ProjectConfig.as_tuple()'s JSON shape",
           reshaped == {"env:foo": {"board": "b", "build_flags": ["-DA=1"]}})

    # non-env sections (platformio, libs, esp32, esp32_s3, ...) are dropped;
    # only env:* sections are part of what actually gets built/flashed.
    mixed = {"esp32_s3": {"upload_command": "x"}, "env:foo": {"board": "b"}}
    report("non-env: sections are filtered out",
           normalize_config(mixed, proj) == {"env:foo": {"board": ["b"]}})

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--generate", action="store_true", help="write the baseline")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    project_dir = str(ROOT)

    try:
        raw_live = live_config(ROOT)
    except Exception as exc:  # noqa: BLE001
        print(f"variant-ini-effective: FAILED to obtain live config -- {exc}",
              file=sys.stderr)
        return 1

    live = normalize_config(raw_live, project_dir)

    if args.generate:
        # The baseline is stored in its RAW (pre-line-split) shape, with only
        # the project-path substitution applied, so it stays a faithful,
        # human-diffable record of what `pio project config --json-output`
        # actually printed -- not a pre-digested comparison artifact.
        raw_env_only = {
            section: {
                key: (
                    [normalize_path(str(v), project_dir) for v in value]
                    if isinstance(value, list)
                    else normalize_path(str(value), project_dir)
                )
                for key, value in options.items()
            }
            for section, options in raw_live.items()
            if section.startswith("env:")
        }
        BASELINE.parent.mkdir(parents=True, exist_ok=True)
        with BASELINE.open("w") as f:
            json.dump(raw_env_only, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"wrote {BASELINE} ({len(raw_env_only)} envs, "
              f"{count_keys(normalize_config(raw_env_only, project_dir))} keys)")
        return 0

    if not BASELINE.exists():
        print(f"no baseline at {BASELINE}; run --generate first", file=sys.stderr)
        return 1

    raw_baseline = json.loads(BASELINE.read_text())
    baseline = normalize_config(raw_baseline, project_dir)

    diffs = compare(baseline, live)
    if not diffs:
        print(f"variant-ini-effective: PASS ({len(baseline)} envs, "
              f"{count_keys(baseline)} keys)")
        return 0

    print(f"variant-ini-effective: FAIL -- {len(diffs)} differing keys",
          file=sys.stderr)
    for line in diffs[:200]:
        print("  " + line, file=sys.stderr)
    if len(diffs) > 200:
        print(f"  ... and {len(diffs) - 200} more", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
