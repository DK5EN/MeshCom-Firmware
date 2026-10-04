"""Unit tests for the pure parts of oled_harness.py -- no hardware, no serial port."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oled_harness as oh  # noqa: E402


def test_module_imports_without_port():
    assert oh.ORDER[0] == "boot" and "dirty" in oh.SCENARIOS
    assert callable(oh.explain_idle_frames) and callable(oh.burst_has_skip)


# ------------------------------------------------------- explain_idle_frames

def test_frame_two_seconds_after_tx_is_explained():
    # run 1: relay TX at 07:56:40, frame 2 s later
    exp, unexp = oh.explain_idle_frames([102.0], [100.0])
    assert exp == [102.0] and unexp == []


def test_frame_without_lora_line_is_unexplained():
    exp, unexp = oh.explain_idle_frames([110.0], [100.0])
    assert exp == [] and unexp == [110.0]


def test_lora_line_after_the_frame_does_not_explain():
    exp, unexp = oh.explain_idle_frames([100.0], [101.0])
    assert exp == [] and unexp == [100.0]


def test_window_boundary_and_mixed():
    exp, unexp = oh.explain_idle_frames([102.5, 102.6, 120.0], [100.0])
    assert exp == [102.5] and unexp == [102.6, 120.0]


def test_empty_inputs():
    assert oh.explain_idle_frames([], [1.0]) == ([], [])
    assert oh.explain_idle_frames([1.0], []) == ([], [1.0])


# ------------------------------------------------------------ lora regex

def test_lora_regex_matches_real_lines():
    assert oh.LORA_RE.search("07:56:39 [LOG] TX xE9F113B7 @ H01 prio=5 src=r wait=17162")
    assert oh.LORA_RE.search("07:46:58 [LOG] RLY x5858112B @ H01 q=tx prio=5 slot=0")
    assert oh.LORA_RE.search("07:46:58 [LOG] 085 @ x5858112B H01 S1 T0 M01")
    assert oh.LORA_RE.search("RX-LoRa something")
    assert not oh.LORA_RE.search("[OLED];frame;us;1234;n;5;page;0;last;0;lines;4")
    assert not oh.LORA_RE.search("07:46:58 [LOG] TXT something")


# ------------------------------------------------------------ burst_has_skip

F = "[OLED];frame;us;9000;n;12;page;0;last;0;lines;5;crc;1a2b3c4d;skipped;3"
S = "[OLED];skip;n;13;skipped;4"


def test_run1_burst_with_skip():
    # run 1: frames at 49.864/50.121/50.932, skip at 51.153
    assert oh.burst_has_skip([F, F, F, S])


def test_run5_three_frames_no_skip():
    assert not oh.burst_has_skip([F, F, F])


def test_empty_burst():
    assert not oh.burst_has_skip([])
