"""tools/regression_select.py: the change-based selection of the host gate (TESTGATE-01).

The selector is only safe when its include closure is a superset of what the compiler
reads. That is checked here against the compiler's own dependency list (`c++ -MM`) for
the header-only native envs, where the flags are fully known from platformio.ini.
"""

from __future__ import annotations

import shlex
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import regression_select as rs  # noqa: E402


@pytest.fixture(scope="module")
def project() -> rs.Project:
    return rs.Project()


def test_self_test_passes() -> None:
    assert rs.self_test() == 0


def test_every_suite_file_selects_the_envs_of_its_suite(project: rs.Project) -> None:
    """Editing any file of a suite runs every env that holds the suite."""
    missing = []
    for env in project.envs:
        for suite in project.env_suites(env):
            first = next(f for f in project.files if f.startswith(f"test/{suite}/"))
            sel = rs.select([first], project)
            if sel["mode"] != "full" and suite not in sel["unity"].get(env, []):
                missing.append((env, suite, first))
    assert not missing, missing[:5]


def test_every_compiled_source_selects_its_env(project: rs.Project) -> None:
    """A src file named in an env's build_src_filter runs that env."""
    missing = []
    for env in project.envs:
        if not project.env_suites(env):
            continue
        for src in project.env_src_files(env):
            if env not in rs.select([src], project)["unity"]:
                missing.append((env, src))
    assert not missing, missing[:5]


def test_a_suite_filter_never_reaches_an_env_without_that_suite(project: rs.Project) -> None:
    """Regression 2026-11-24: `pio test -e A -e B -f x -f y` ran x and y in both envs.

    A change to three suites of five envs gave ten false failures. Every call of a
    selection may filter only for suites that each of its envs holds.
    """
    changed = [
        "test/test_ack_replay/test_ack_replay.cpp",
        "test/test_nbr_replay/test_nbr_replay.cpp",
        "test/test_topo_shadow/test_topo_shadow.cpp",
    ]
    sel = rs.select(changed, project)
    assert sel["mode"] == "select"
    groups = sel["unity_groups"]
    assert sorted(e for envs, _ in groups for e in envs) == sorted(sel["unity"])
    for envs, suites in groups:
        for env in envs:
            if suites:
                assert set(suites) <= set(project.env_suites(env)), (env, suites)
                assert sorted(suites) == sel["unity"][env]
            else:
                assert sel["unity"][env] == sorted(project.env_suites(env)), env
    assert (["native"], ["test_ack_replay"]) in groups
    line = next(ln for ln in rs.shell_lines(sel) if ln.startswith("UNITY_GROUPS="))
    assert "native:test_ack_replay" in line


def test_every_native_env_has_a_suite(project: rs.Project) -> None:
    """An env whose test_filter matches no directory would silently run nothing."""
    assert [e for e in project.envs if not project.env_suites(e)] == []


def test_every_suite_is_in_an_env(project: rs.Project) -> None:
    """A test_* directory with sources that no env runs is an ungated suite."""
    in_env = {s for e in project.envs for s in project.env_suites(e)}
    with_sources = {
        s for s in project.suites
        if any(f.startswith(f"test/{s}/") and f.endswith((".c", ".cpp")) for f in project.files)
    }
    # test_compress holds only inert upstream copies (compress_functions.*), no suite of ours.
    assert sorted(with_sources - in_env - {"test_compress"}) == []


def _define_and_include_flags(build_flags: str) -> list[str]:
    """-D/-I/-std flags of a build_flags value; "-D NAME" / "-I dir" keep their separate argument."""
    tokens: list[str] = []
    for line in build_flags.splitlines():
        line = line.split(";", 1)[0].strip()  # ini comment
        if line:
            tokens += shlex.split(line)
    flags: list[str] = []
    it = iter(tokens)
    for tok in it:
        if tok in ("-D", "-I"):
            arg = next(it, None)
            if arg is not None:
                flags += [tok, arg]
        elif tok.startswith(("-D", "-I", "-std")):
            flags.append(tok)
    return flags


def _compiler_deps(project: rs.Project, env: str, source: str) -> set[str] | None:
    """Repo-relative files the compiler reads for *source* under the env's flags, or None."""
    flags = _define_and_include_flags(project.opt(env, "build_flags"))
    unity = sorted(ROOT.glob(".pio/libdeps/native*/Unity/src/unity.h"))  # present after one gate run
    if not unity:
        return None
    cmd = ["c++", "-MM", *flags, "-I", str(unity[0].parent), "-I", f"test/{source.split('/')[1]}", source]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        return None
    deps = set()
    for token in proc.stdout.replace("\\\n", " ").split()[1:]:
        path = (ROOT / token).resolve()
        if path.is_relative_to(ROOT) and ".pio" not in path.parts:
            deps.add(path.relative_to(ROOT).as_posix())
    return deps


@pytest.mark.skipif(shutil.which("c++") is None, reason="no host compiler")
def test_closure_covers_what_the_compiler_reads(project: rs.Project) -> None:
    """Header-only envs: every file `c++ -MM` names is inside the selector's closure."""
    checked = 0
    gaps = []
    for env in project.envs:
        if project.opt(env, "test_build_src").strip().lower() in ("yes", "true", "1"):
            continue
        if project.opt(env, "extra_scripts") or project.opt(env, "lib_deps"):
            continue
        for suite in project.env_suites(env):
            closure = project.suite_closure(env, suite)
            for source in sorted(f for f in closure if f.startswith(f"test/{suite}/") and f.endswith(".cpp")):
                deps = _compiler_deps(project, env, source)
                if deps is None:
                    continue
                checked += 1
                gaps += [(env, source, d) for d in sorted(deps - closure)]
    assert not gaps, gaps[:10]
    # A broken lookup (no Unity header, wrong flags) must not pass as "no gap found".
    assert checked >= 40, f"only {checked} translation units could be checked"


def test_unknown_and_configuration_paths_select_the_full_gate(project: rs.Project) -> None:
    for path in ("platformio.ini", "variants/t_echo/platformio.ini", "tools/regression.sh",
                 "tools/regression_select.py", "brand/new/file.xyz", "test/support/new_fixture.bin"):
        assert rs.select([path], project)["mode"] == "full", path


def test_one_unknown_path_is_not_outvoted(project: rs.Project) -> None:
    sel = rs.select(["docs/RESUME.md", "src/rtc_offset.h", "brand/new/file.xyz"], project)
    assert sel["mode"] == "full"


def test_shell_output_is_evaluable(project: rs.Project) -> None:
    lines = rs.shell_lines(rs.select(["src/rtc_offset.h"], project))
    assign = [line for line in lines if not line.startswith("#")]
    assert any(line == "MODE=select" for line in assign)
    assert all("=" in line and "`" not in line and "$(" not in line for line in assign)
