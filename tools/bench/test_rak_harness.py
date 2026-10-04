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
