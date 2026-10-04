"""Host tests for rak_harness.py helpers (the scenarios themselves need the RAK)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import rak_harness as rh  # noqa: E402


def test_restore_ip_arg_clears_with_none_for_empty_and_none():
    # the pre-state of a cleared node is an empty field, never send that back
    assert rh._restore_ip_arg("") == "none"
    assert rh._restore_ip_arg(None) == "none"
    assert rh._restore_ip_arg("none") == "none"
    assert rh._restore_ip_arg("  ") == "none"


def test_restore_ip_arg_keeps_a_real_address():
    assert rh._restore_ip_arg("192.168.68.74") == "192.168.68.74"
    assert rh._restore_ip_arg("mcapp.local") == "mcapp.local"


def test_crash_pattern_is_word_bounded():
    import re
    beacon = ("00:00:27 [LOG] 123 ! xF6A5B173 H00 S1 T0 M01 DB0ISM-1,DB0ED-99,DK5EN-98>*!4813.24N/"
              "01139.99ErWasserturm Ismaning/B=080/A=001617/N8/R=20;262;2628;26283;9; HW:43")
    assert re.search(rh.CRASH, beacon) is None
    assert re.search(rh.CRASH, "assert failed: foo bar") is not None
    assert re.search(rh.CRASH, "assertion 'x' failed") is not None
    assert re.search(rh.CRASH, "<HardFault> at 0x1234") is not None
    assert re.search(rh.CRASH, "[BOOT] RESETREAS=0x00000002") is not None
    assert re.search(rh.CRASH, "[BOOT] RESETREAS=0x00000004") is None
