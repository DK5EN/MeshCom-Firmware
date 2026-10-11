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


# ------------------------------------------------------------------ analyzer on synthetic runs (RM1 repeats, late copies)
import datetime as dt
import json
import tempfile
from typing import Dict, List, Optional

BASE = dt.datetime(2026, 10, 11, 9, 0, 0)
TAG = "993e4f7e890e7b10"
TAG2 = "29612b6b8cad4f75"
TAIL = "HW:43 MOD:8/8 FCS:10FA FW:40:a LH:AB"


def _l(sec: int, text: str) -> str:
    return "%s %s\n" % ((BASE + dt.timedelta(seconds=sec)).strftime("%H:%M:%S"), text)


def _cmd(tag: str, ctr: int, mid: str, kind: str = "NEW-TXT") -> str:
    return "%s 070 : x%s H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 %d status %s %s" % (kind, mid, ctr, tag, TAIL)


def _rx_cmd(mid: str, ctr: int) -> str:
    return "[LOG] 070 : x%s H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 %d status %s %s RSSI:-45 SNR:6 DUP:n OWN:- t=1" % (mid, ctr, TAG, TAIL)


def _rx_rep(mid: str, ctr: int) -> str:
    return "[LOG] 085 : x%s H04 S0 T0 M00 DK5EN-90>DK5EN-1:RM1 %d ok v=4.40a %s %s RSSI:-45 SNR:6 DUP:n OWN:- t=3" % (mid, ctr, TAG2, TAIL)


def _make_run(h1: List[str], h90: List[str], extra: Optional[Dict[str, object]] = None, case: str = "rm:status") -> str:
    tmp = tempfile.mkdtemp(prefix="gwb_test_")
    t0 = BASE.timestamp()
    ev: Dict[str, object] = {"case": case, "dir": "1>90", "result": "ok", "lat": 3.0, "ver": 1, "reply": "ok v=4.40a", "t_send": t0 + 10, "ok_d": 1, "rej_d": 0}
    ev.update(extra or {})
    json.dump([{"case": "web_identity", "dir": "DK5EN-1", "host": "192.168.68.71", "result": "ok", "t": t0}, ev], open(tmp + "/events.json", "w"))
    open(tmp + "/DK5EN-1.log", "w").write("".join(h1))
    open(tmp + "/DK5EN-90.log", "w").write("".join(h90))
    return tmp


def _analyze(d: str) -> subprocess.CompletedProcess:
    return _run("gwanalyze.py", d)


def _base_logs() -> "tuple[List[str], List[str]]":
    """Clean command: copy 1 at +10 s executes, reply verified at +13 s."""
    h1 = [_l(10, "[RM];send;DK5EN-90;ctr;7"), _l(10, _cmd(TAG, 7, "EA25A006")), _l(10, _cmd(TAG, 7, "EA25A006", "TX-LoRa")),
          _l(13, _rx_rep("91A434CE", 7)), _l(13, "[RM];reply;DK5EN-90;ctr;7;verified;1")]
    h90 = [_l(11, _rx_cmd("EA25A006", 7)), _l(11, "[RM];ok;ctr;7"), _l(12, "NEW-TXT 085 : x91A434CE H04 S0 T0 M00 DK5EN-90>DK5EN-1:RM1 7 ok v=4.40a %s %s" % (TAG2, TAIL))]
    return h1, h90


def test_audit_clean_run_passes() -> None:
    h1, h90 = _base_logs()
    r = _analyze(_make_run(h1, h90))
    assert r.returncode == 0 and "FAIL" not in r.stdout.replace("0 FAIL", ""), r.stdout[-1500:]
    assert "audit: 1 command(s), 0 FAIL" in r.stdout


def test_audit_second_execution_fails() -> None:
    h1, h90 = _base_logs()
    h90 += [_l(50, _rx_cmd("EA25A406", 7)), _l(50, "[RM];ok;ctr;7")]
    r = _analyze(_make_run(h1, h90))
    assert r.returncode == 1 and "FAIL rm:status 1>90 ctr 7: second execution" in r.stdout, r.stdout[-1500:]


def test_audit_late_copy_listed_but_not_fail() -> None:
    h1, h90 = _base_logs()
    h90 += [_l(55, _rx_cmd("EA25A406", 7)), _l(55, "[RM];cached;ctr;7")]
    h1 += [_l(56, _rx_rep("91A43555", 7))]
    r = _analyze(_make_run(h1, h90))
    assert r.returncode == 0, r.stdout[-1500:]
    assert "late copies rm:status 1>90 ctr 7: command [rf@+45s(cached)]" in r.stdout, r.stdout[-1500:]
    assert "reply [rf@+46s]" in r.stdout, r.stdout[-1500:]


def test_audit_stray_repeat_after_stop_fails() -> None:
    h1, h90 = _base_logs()
    h1 += [_l(52, "[RETX] PNRETRY k=1 msg-id:EA25A406"), _l(52, _cmd(TAG, 7, "EA25A406", "TX-LoRa"))]
    r = _analyze(_make_run(h1, h90))
    assert r.returncode == 1 and "stray repeat after the stop" in r.stdout, r.stdout[-1500:]


def test_audit_repeat_before_stop_is_not_stray() -> None:
    h1, h90 = _base_logs()
    # reply lost once: copy 2 at +52 s, verified at +55 s; the copy sits before the stop
    h1 = [x for x in h1 if "verified" not in x and "91A434CE" not in x]
    h1 += [_l(52, "[RETX] PNRETRY k=1 msg-id:EA25A406"), _l(52, _cmd(TAG, 7, "EA25A406", "TX-LoRa")), _l(55, _rx_rep("91A43599", 7)),
           _l(55, "[RM];reply;DK5EN-90;ctr;7;verified;1"), _l(55, "[RM];stop;ctr;7")]
    h90 += [_l(53, _rx_cmd("EA25A406", 7)), _l(53, "[RM];cached;ctr;7")]
    r = _analyze(_make_run(h1, h90, {"expect": {"min_repeats": 1, "exec": 1, "min_cached": 1}}, case="repeat:lost-reply"))
    assert r.returncode == 0, r.stdout[-1800:]


def _inflight_logs(tx_after_stop: List[str]) -> "tuple[List[str], List[str]]":
    """Copies 2 (+40 s) and 3 (+41 s) enqueued before the stop; verified at +45 s (reply to copy 2) with the stop marker."""
    h1, h90 = _base_logs()
    h1 = [x for x in h1 if "verified" not in x and "91A434CE" not in x]
    h1 += [_l(40, "[RETX] RM1RETRY k=1 msg-id:EA25A406"), _l(40, _cmd(TAG, 7, "EA25A406", "TX-LoRa")),
           _l(41, "[RETX] RM1RETRY k=2 msg-id:EA25A806"),
           _l(45, _rx_rep("91A43599", 7)), _l(45, "[RM];reply;DK5EN-90;ctr;7;verified;1"), _l(45, "[RM];stop;ctr;7;slot;2")]
    h1 += tx_after_stop
    h90 += [_l(41, _rx_cmd("EA25A406", 7)), _l(41, "[RM];cached;ctr;7")]
    return h1, h90


def test_audit_one_inflight_copy_after_stop_passes() -> None:
    h1, h90 = _inflight_logs([_l(46, _cmd(TAG, 7, "EA25A806", "TX-LoRa"))])
    r = _analyze(_make_run(h1, h90, {"expect": {"min_repeats": 1, "exec": 1, "min_cached": 1}}, case="repeat:lost-reply"))
    assert r.returncode == 0 and "stray repeat" not in r.stdout, r.stdout[-1800:]


def test_audit_two_copies_after_stop_fail() -> None:
    h1, h90 = _inflight_logs([_l(46, _cmd(TAG, 7, "EA25A806", "TX-LoRa")), _l(47, _cmd(TAG, 7, "EA25AC06", "TX-LoRa"))])
    r = _analyze(_make_run(h1, h90, {"expect": {"min_repeats": 1, "exec": 1, "min_cached": 1}}, case="repeat:lost-reply"))
    assert r.returncode == 1 and "stray repeat after the stop" in r.stdout and "one in-flight copy is allowed" in r.stdout, r.stdout[-1800:]


def test_audit_enqueue_after_stop_fails_even_with_old_stop_line() -> None:
    h1, h90 = _inflight_logs([_l(46, "[RETX] RM1RETRY k=3 msg-id:EA25AC06")])
    r = _analyze(_make_run(h1, h90, {"expect": {"min_repeats": 1, "exec": 1, "min_cached": 1}}, case="repeat:lost-reply"))
    assert r.returncode == 1 and "stray repeat after the stop" in r.stdout and "enqueued" in r.stdout, r.stdout[-1800:]


def test_audit_old_stop_line_without_slot_still_parsed() -> None:
    h1, h90 = _inflight_logs([_l(46, "[RETX] RM1RETRY k=3 msg-id:EA25AC06")])
    h1 = [x.replace(";stop;ctr;7;slot;2", ";stop;ctr;7") for x in h1]
    r = _analyze(_make_run(h1, h90, {"expect": {"min_repeats": 1, "exec": 1, "min_cached": 1}}, case="repeat:lost-reply"))
    assert r.returncode == 1 and "stray repeat after the stop (marker)" in r.stdout, r.stdout[-1800:]


def test_audit_second_verified_row_and_strike_fail() -> None:
    h1, h90 = _base_logs()
    h1 += [_l(60, "[RM];reply;DK5EN-90;ctr;7;verified;1")]
    h90 += [_l(30, "[RM];reject;tag")]
    r = _analyze(_make_run(h1, h90))
    assert r.returncode == 1 and "second verified row" in r.stdout and "strike after the command" in r.stdout, r.stdout[-1500:]


def test_audit_replay_and_rate_rejects_are_no_strike() -> None:
    h1, h90 = _base_logs()
    h90 += [_l(20, "[RM];reject;replay"), _l(21, "[RM];reject;rate")]
    r = _analyze(_make_run(h1, h90))
    assert r.returncode == 0, r.stdout[-1500:]


# ------------------------------------------------------------------ driver plans
def test_dry_run_lora_only_plan() -> None:
    r = _run("gwrun.py", "--dry-run", "--lora-only")
    assert r.returncode == 0 and "LoRa only" in r.stdout and "'setup'" not in r.stdout and "no mock, no --srvip" in r.stdout, r.stdout + r.stderr


def test_dry_run_default_keeps_gateway_plan() -> None:
    r = _run("gwrun.py", "--dry-run")
    assert r.returncode == 0 and "'setup'" in r.stdout and "gateways against the mock" in r.stdout, r.stdout + r.stderr


def test_dry_run_repeat_plan() -> None:
    r = _run("gwrun.py", "--dry-run", "--phases", "guard,repeat")
    assert r.returncode == 0 and "a lost command" in r.stdout and "b lost reply" in r.stdout and "--airgap off on both nodes" in r.stdout, r.stdout + r.stderr
    assert "'setup'" not in r.stdout


def test_audit_counts_rm1retry_lines() -> None:
    h1, h90 = _base_logs()
    h1 = [x for x in h1 if "verified" not in x and "91A434CE" not in x]
    h1 += [_l(52, "[RETX] RM1RETRY k=1 msg-id:EA25A406"), _l(55, _rx_rep("91A43599", 7)), _l(55, "[RM];reply;DK5EN-90;ctr;7;verified;1")]
    h90 += [_l(53, _rx_cmd("EA25A406", 7)), _l(53, "[RM];cached;ctr;7")]
    r = _analyze(_make_run(h1, h90, {"expect": {"min_repeats": 1, "exec": 1, "min_cached": 1}}, case="repeat:lost-reply"))
    assert r.returncode == 0 and "no [RETX] PNRETRY|RM1RETRY lines" not in r.stdout, r.stdout[-1800:]
    assert "expected >= 1 repeat" not in r.stdout


def test_dry_run_adds_guard_phase() -> None:
    r = _run("gwrun.py", "--dry-run", "--phases", "repeat")
    assert r.returncode == 0 and "phase guard added" in r.stdout and "['guard', 'repeat']" in r.stdout, r.stdout + r.stderr
    r = _run("gwrun.py", "--dry-run", "--phases", "guard,refused")
    assert r.returncode == 0 and "phase guard added" not in r.stdout, r.stdout + r.stderr


def test_txpower_guard_parses_defensively() -> None:
    import types
    sys.path.insert(0, str(HERE))
    import gwrun
    import pytest
    run = gwrun.Run.__new__(gwrun.Run)
    dst = types.SimpleNamespace(call="DK5EN-90", pw="x")
    for bad in ("3", " 22 ", "abc", "1.5", "", "2 3"):
        with pytest.raises(gwrun.Abort):
            run.rm_post(None, dst, "txpower", bad)  # type: ignore[arg-type]
