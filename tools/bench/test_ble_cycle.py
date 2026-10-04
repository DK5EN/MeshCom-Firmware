"""Host tests for tools/bench/ble_cycle.py -- the pure parts (argument validation,
advert match, delay and connect summaries, exit code). No BLE, no network."""
from __future__ import annotations

import asyncio
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ble_cycle as bc  # noqa: E402


def args(*argv: str):
    return bc.build_parser().parse_args(list(argv))


# ---------------------------------------------------------------- validate_args


def test_defaults_with_name_are_valid():
    assert bc.validate_args(args("--name", "DK5EN-1")) == []


def test_name_or_address_required():
    assert bc.validate_args(args()) == ["--name or --address"]
    assert bc.validate_args(args("--address", "AA:BB")) == []


def test_gap_zero_is_valid_and_negative_is_not():
    assert bc.validate_args(args("--name", "X", "--gap", "0")) == []
    errs = bc.validate_args(args("--name", "X", "--gap", "-1"))
    assert any("--gap" in e for e in errs)


@pytest.mark.parametrize(
    "flag,value,needle",
    [
        ("--cycles", "0", "--cycles"),
        ("--hold", "-1", "--hold"),
        ("--scan-seconds", "0", "--scan-seconds"),
        ("--attempts", "0", "--attempts"),
        ("--retry-delay", "-0.1", "--retry-delay"),
        ("--abort-ms", "-5", "--abort-ms"),
        ("--pin", "1000000", "--pin"),
        ("--pin", "-1", "--pin"),
    ],
)
def test_out_of_range_values_rejected(flag, value, needle):
    errs = bc.validate_args(args("--name", "X", flag, value))
    assert any(needle in e for e in errs)


def test_pin_zero_is_valid():
    assert bc.validate_args(args("--name", "X", "--pin", "0")) == []


def test_drop_unclean_accepted_and_excludes_send():
    assert bc.validate_args(args("--name", "X", "--drop", "unclean")) == []
    errs = bc.validate_args(args("--name", "X", "--drop", "unclean", "--send=--info"))
    assert any("--send" in e for e in errs)


def test_drop_rejects_unknown_value():
    with pytest.raises(SystemExit):
        args("--name", "X", "--drop", "bogus")


# ---------------------------------------------------------------- advert_matches


def test_match_exact_callsign_suffix():
    assert bc.advert_matches("MC-ab12-DK5EN-1", None, "u", "DK5EN-1", "")


def test_match_is_case_insensitive():
    assert bc.advert_matches("mc-ab12-dk5en-1", None, "u", "DK5EN-1", "")


def test_match_does_not_pick_longer_ssid():
    assert not bc.advert_matches("MC-ab12-DK5EN-14", None, "u", "DK5EN-1", "")


def test_match_falls_back_to_local_name():
    assert bc.advert_matches(None, "MC-ab12-DK5EN-1", "u", "DK5EN-1", "")
    assert bc.advert_matches("", "MC-ab12-DK5EN-1", "u", "DK5EN-1", "")


def test_match_no_names_never_matches():
    assert not bc.advert_matches(None, None, "u", "DK5EN-1", "")
    assert not bc.advert_matches("MC-x-DK5EN-1", None, "u", "", "")


def test_match_address_wins_over_name():
    assert bc.advert_matches("MC-x-OTHER-9", None, "aa-bb", "DK5EN-1", "AA-BB")
    assert not bc.advert_matches("MC-x-DK5EN-1", None, "cc-dd", "DK5EN-1", "AA-BB")


# ---------------------------------------------------------------- summaries


def test_summarize_delays_basic():
    s = bc.summarize_delays([120.0, 80.0, 400.0])
    assert (s["min"], s["median"], s["max"]) == (80.0, 120.0, 400.0)
    assert (s["n"], s["seen"], s["missed"]) == (3, 3, 0)


def test_summarize_delays_even_count_median_and_missed():
    s = bc.summarize_delays([100.0, None, 300.0, 200.0, 400.0])
    assert s["median"] == 250.0
    assert (s["n"], s["seen"], s["missed"]) == (5, 4, 1)


def test_summarize_delays_empty_and_all_missed():
    for data in ([], [None, None]):
        s = bc.summarize_delays(data)
        assert s["min"] is None and s["median"] is None and s["max"] is None
        assert s["seen"] == 0 and s["missed"] == len(data)


def test_summarize_connects_classes():
    s = bc.summarize_connects(
        [
            [True],  # clean
            [False, True],  # first attempt only
            [False, False, True],  # first attempt only
            [False, False, False],  # failed
            [],  # never found: failed
            [True, False],  # impossible in run() (stops on success) but must not count
        ]
    )
    assert s == {"cycles": 6, "failed": 2, "first_only": 2}


def test_summarize_connects_empty():
    assert bc.summarize_connects([]) == {"cycles": 0, "failed": 0, "first_only": 0}


# ---------------------------------------------------------------- exit code / text


def clean_delays():
    return bc.summarize_delays([100.0, 200.0])


def test_exit_code_tolerates_first_attempt_only():
    assert bc.exit_code(clean_delays(), {"cycles": 2, "failed": 0, "first_only": 2}, 0) == 0


def test_exit_code_fails_on_failed_connect_missed_advert_or_session_failure():
    ok = {"cycles": 2, "failed": 0, "first_only": 0}
    assert bc.exit_code(clean_delays(), {**ok, "failed": 1}, 0) == 1
    assert bc.exit_code(bc.summarize_delays([100.0, None]), ok, 0) == 1
    assert bc.exit_code(clean_delays(), ok, 1) == 1


def test_format_summary_lines():
    lines = bc.format_summary(
        bc.summarize_delays([100.0, 300.0, None]),
        {"cycles": 3, "failed": 1, "first_only": 2},
        0,
    )
    assert lines[0] == "=== advert delay ms: min=100 median=200 max=300 seen=2/3 missed=1"
    assert lines[1] == "=== connects: cycles=3 failed=1 first_attempt_only_failed=2 other_failures=0"


def test_format_summary_no_data():
    lines = bc.format_summary(bc.summarize_delays([]), bc.summarize_connects([]), 0)
    assert "min=n/a median=n/a max=n/a" in lines[0]


# ---------------------------------------------------------------- frames (unchanged)


def test_frames():
    assert bc.text_frame("--info") == bytes([8, 0xA0]) + b"--info"
    assert bc.hello(None) == bytes([4, 0x10, 0x20, 0x30])
    assert len(bc.hello(123456)) == 4 + 32


# ---------------------------------------------------------------- retry classification (mocked sessions)


class FakeDev:
    address = "fake-uuid"


def drive(monkeypatch, script, *extra, advert_ms=100.0):
    """Run bc.run() with find_node/wait_for_advert/connect_session mocked.

    script: list of per-call outcomes for connect_session, consumed in order; an entry is
    True/False (session ok / link dropped but no exception) or an Exception instance to raise.
    Returns (exit code, number of connect_session calls).
    """
    calls = {"n": 0}

    async def fake_find(_a):
        return FakeDev()

    async def fake_advert(_a, _t0):
        return advert_ms, FakeDev()

    async def fake_session(_a, _dev, _i):
        r = script[calls["n"]]
        calls["n"] += 1
        if isinstance(r, Exception):
            raise r
        return r

    monkeypatch.setattr(bc, "find_node", fake_find)
    monkeypatch.setattr(bc, "wait_for_advert", fake_advert)
    monkeypatch.setattr(bc, "connect_session", fake_session)
    a = args("--name", "X", "--gap", "0", "--retry-delay", "0", *extra)
    return asyncio.run(bc.run(a)), calls["n"]


def test_all_clean_exits_0(monkeypatch):
    assert drive(monkeypatch, [True, True], "--cycles", "2") == (0, 2)


def test_post_connect_error_is_not_retried_and_fails_run(monkeypatch):
    # attempt budget 3, but a BleakError-like error after the link was up must not be retried
    rc, calls = drive(monkeypatch, [OSError("link lost during hold"), True, True], "--cycles", "1")
    assert (rc, calls) == (1, 1)


def test_link_dropped_without_exception_fails_run(monkeypatch):
    assert drive(monkeypatch, [False], "--cycles", "1") == (1, 1)


def test_connect_error_then_success_is_first_only_and_exits_0(monkeypatch, capsys):
    rc, calls = drive(monkeypatch, [bc.ConnectFailed("0x3E"), True], "--cycles", "1")
    assert (rc, calls) == (0, 2)
    out = capsys.readouterr().out
    assert "first_attempt_only_failed=1" in out and "failed=0" in out


def test_all_connect_attempts_failing_exits_1(monkeypatch, capsys):
    rc, calls = drive(monkeypatch, [bc.ConnectFailed("a")] * 3, "--cycles", "1")
    assert (rc, calls) == (1, 3)
    assert "failed=1" in capsys.readouterr().out


def test_connect_ok_after_retry_then_session_error_fails_run(monkeypatch):
    rc, calls = drive(monkeypatch, [bc.ConnectFailed("a"), OSError("dropped"), True], "--cycles", "1")
    assert (rc, calls) == (1, 2)


def test_missed_advert_fails_run(monkeypatch):
    assert drive(monkeypatch, [True], "--cycles", "1", advert_ms=None) == (1, 1)


def test_attempt_cycle_returns_outcomes():
    async def boom(_a, _d, _i):
        raise OSError("x")

    async def refuse(_a, _d, _i):
        raise bc.ConnectFailed("x")

    a = args("--name", "X", "--retry-delay", "0")
    assert asyncio.run(bc.attempt_cycle(a, FakeDev(), 1, boom)) == ([True], False)
    assert asyncio.run(bc.attempt_cycle(a, FakeDev(), 1, refuse)) == ([False, False, False], True)
