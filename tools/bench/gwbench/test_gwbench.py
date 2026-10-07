"""Offline checks of the gateway bench toolbox (no hardware, no network)."""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-I", *args], cwd=HERE, capture_output=True, text=True, timeout=120)


def test_guard_selftest() -> None:
    r = _run("gwguard.py")
    assert r.returncode == 0 and "gwguard selftest OK" in r.stdout, r.stdout + r.stderr


def test_analyzer_selftest() -> None:
    r = _run("gwanalyze.py", "--selftest")
    assert r.returncode == 0 and "selftest ok" in r.stdout, r.stdout[-400:] + r.stderr


def test_driver_dry_run_touches_nothing() -> None:
    r = _run("gwrun.py", "--dry-run")
    assert r.returncode == 0 and "DRY RUN" in r.stdout, r.stdout + r.stderr


def test_driver_refuses_foreign_destinations() -> None:
    r = _run("-c", "import sys; sys.path.insert(0, '.'); import gwrun; gwrun.guard_call('DK5EN-98')")
    assert r.returncode != 0 and "Abort" in r.stderr, r.stderr
