"""Host tests for the verdict parser of au_gate_burst.py (lines as logged by DK5EN-92, 2026-11-06)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from au_gate_burst import read_round, read_verdict

STARTED = "[AU];check;started;chan;dev"
TAG = "[AU];check;tag;v4.40a.11.01;newer;1"


def test_refusal_is_a_failure() -> None:
    assert (
        read_round([STARTED, "[AU];refuse;lowheap;ifree;73572", "[AU];fail;lowheap_ble_off"])
        == "lowheap_ble_off"
    )


def test_restart_then_pass_counts_as_pass_and_restart() -> None:
    v = read_verdict([[STARTED, "[AU];shed;restart", TAG], [STARTED, TAG]])
    assert (v.passed, v.restarted, v.failed) == (2, 1, ())


def test_silent_round_is_a_failure() -> None:
    assert read_round([STARTED]) == "no verdict"
    assert read_round([]) == "not started"


def test_refused_command_is_a_failure() -> None:
    assert read_round(["[ERR];update;busy"]) == "not started"


def test_reboot_is_a_failure_even_with_a_tag() -> None:
    assert read_round([STARTED, TAG, "[BOOT] RESET_REASON=4 PANIC"]) == "reboot"


def test_scheduled_check_cannot_mask_a_silent_round() -> None:
    v = read_verdict([[], [STARTED, TAG, TAG]])
    assert (v.passed, v.failed) == (1, ("not started",))
