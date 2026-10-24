"""Tests for dtr_replay.py -- fake serial port only, no hardware."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dtr_replay as dr


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now


class FakeSerial:
    """Records dtr/rts assignments, close and open with timestamps; feeds
    scripted lines. Answers --info / --pos like the node would."""

    def __init__(self, clock: FakeClock, mode: str, world: "World") -> None:
        self.clock = clock
        self.events = world.events
        self.dtr = False
        self.rts = False
        self.world = world
        self.events.append(("open", mode, clock.now))

    def __setattr__(self, name: str, value: Any) -> None:
        object.__setattr__(self, name, value)
        if name in ("dtr", "rts") and "world" in self.__dict__:
            self.events.append((name, bool(value), self.clock.now))
            self.world.transitions.append((int(self.dtr), int(self.rts)))

    def write(self, data: bytes) -> int:
        for cmd in data.decode().split("\n"):
            if cmd == "--info":
                self.world.script.append((self.clock.now, b"...Call: <TEST-1>\n"))
            elif cmd == "--pos":
                self.world.script.append(
                    (self.clock.now, f"...Track: {self.world.track}\n".encode())
                )
            elif cmd == "--track off":
                self.world.track = "off"
                self.world.sent.append(cmd)
            elif cmd:
                self.world.sent.append(cmd)
        return len(data)

    def flush(self) -> None:
        pass

    def read(self, n: int) -> bytes:
        out = b""
        rest = []
        for t, chunk in self.world.script:
            if t <= self.clock.now:
                out += chunk
            else:
                rest.append((t, chunk))
        self.world.script[:] = rest
        self.clock.now += 0.05
        return out

    def close(self) -> None:
        self.events.append(("close", None, self.clock.now))


class World:
    def __init__(self, clock: FakeClock, track: str = "off") -> None:
        self.clock = clock
        self.track = track
        self.events: list[tuple[str, Any, float]] = []
        self.transitions: list[tuple[int, int]] = []
        self.script: list[tuple[float, bytes]] = []
        self.sent: list[str] = []

    def opener(self, mode: str) -> FakeSerial:
        return FakeSerial(self.clock, mode, self)


def make_session(track: str = "off") -> tuple[dr.Session, World]:
    clock = FakeClock()
    world = World(clock, track)
    s = dr.Session(world.opener, clock)
    world.transitions.clear()
    return s, world


def test_classic50_transitions() -> None:
    s, w = make_session()
    s.run_steps(dr.STRATEGIES["classic50"])
    assert w.transitions == [(0, 0), (0, 1), (0, 1), (1, 1), (1, 0), (1, 0), (0, 0)]


def test_classic550_differs_only_in_delay() -> None:
    sleeps = [x[1] for x in dr.STRATEGIES["classic550"] if x[0] == "sleep"]
    assert sleeps == [100, 550]
    assert [x[1] for x in dr.STRATEGIES["classic50"] if x[0] == "sleep"] == [100, 50]


def test_usbjtag_transitions() -> None:
    s, w = make_session()
    s.run_steps(dr.STRATEGIES["usbjtag"])
    # Each setRTS is two assignments (rts, then dtr = last), each dtr/rts one.
    assert w.transitions == [
        (0, 0),
        (0, 0),  # setRTS(0); setDTR(0) after last=0 -> rts, dtr
        (0, 0),  # setDTR(0)
        (1, 0),  # setDTR(1)
        (1, 0),
        (1, 0),  # setRTS(0)
        (1, 1),
        (1, 1),  # setRTS(1)
        (0, 1),  # setDTR(0)
        (0, 1),
        (0, 1),  # setRTS(1)
        (0, 0),
        (0, 0),  # setRTS(0)
        (0, 0),  # setDTR(0)
    ]


def test_wa_transitions() -> None:
    s, w = make_session()
    s.run_steps(dr.STRATEGIES["wa"])
    assert w.transitions == [(0, 1), (0, 1), (0, 0), (0, 0)]
    assert dr.STRATEGIES["console_reset"] == dr.STRATEGIES["wa"]


def test_click_train_transitions() -> None:
    s, w = make_session()
    s.run_steps(dr.STRATEGIES["click_train"])
    assert w.transitions == [(0, 0)] + [(1, 0), (0, 0)] * 3


def test_hardreset_and_rts_then_dtr() -> None:
    s, w = make_session()
    s.run_steps(dr.STRATEGIES["hardreset"])
    assert w.transitions == [(0, 1), (0, 1), (0, 0), (0, 0)]
    s2, w2 = make_session()
    s2.run_steps(dr.STRATEGIES["rts_then_dtr"])
    assert w2.transitions == [(1, 0), (1, 0), (1, 0), (0, 0)]


def test_open_close_reopens_default_then_safe() -> None:
    s, w = make_session()
    w.events.clear()
    s.run_steps(dr.STRATEGIES["open_close"])
    kinds = [(e[0], e[1]) for e in w.events]
    assert kinds == [("close", None), ("open", "default"), ("close", None), ("open", "safe")] * 3
    # the default open must not pre-set dtr/rts
    assert not any(e[0] in ("dtr", "rts") for e in w.events)


def test_safe_open_precedes_everything() -> None:
    _, w = make_session()
    assert w.events[0][:2] == ("open", "safe")


def test_step_timestamps_follow_sleeps() -> None:
    s, _ = make_session()
    s.run_steps(dr.STRATEGIES["hardreset"])
    t = [r["t_ms"] for r in s.steps]
    assert t == sorted(t)
    assert t[-1] - t[0] >= 100


def test_all_excludes_click_train() -> None:
    assert "click_train" not in dr.ALL_STRATEGIES
    assert set(dr.ALL_STRATEGIES) | {"click_train"} == set(dr.STRATEGIES)


def test_parse_track() -> None:
    assert dr.parse_track("MeshCom 4.40\n...GPS: on\n...Track: off\n...SOFTSER: off") is False
    assert dr.parse_track("...Track: ON") is True
    assert dr.parse_track('{"TYP":"SN","GPS":true,"TRACK":true,"UTCOF":1}') is True
    assert dr.parse_track("...Track: on\n...Track: off") is False
    assert dr.parse_track("...Call: <X>\nno track here") is None


def test_count_events() -> None:
    lines = [
        "BUTTON single press last:0",
        "TripleClick",
        "Track on - warning",
        "rst:0x1 (POWERON),boot:0x13 (SPI_FAST_FLASH_BOOT)",
        "waiting for download",
        "RESET_REASON=1",
        "unrelated",
    ]
    c = dr.count_events(lines)
    assert c["BUTTON single"] == 1 and c["TripleClick"] == 1 and c["Track on"] == 1
    assert c["boot:"] == 1 and c["waiting for download"] == 1 and c["RESET_REASON"] == 1
    assert dr.button_events(c) == 3
    assert dr.button_events(dr.count_events(["boot:0x13", "RESET_REASON=1"])) == 0


def test_verdict_logic() -> None:
    none = dr.count_events([])
    click = dr.count_events(["TripleClick"])
    assert dr.run_verdict(none, False, False, False) == "ok"
    assert dr.run_verdict(click, False, False, False) == "finding"
    assert dr.run_verdict(none, False, True, False) == "finding"
    assert dr.run_verdict(click, False, True, True) == "control_ok"
    assert dr.run_verdict(none, False, False, True) == "control_failed"
    assert dr.exit_code([{"verdict": "ok"}]) == 0
    assert dr.exit_code([{"verdict": "ok"}, {"verdict": "finding"}]) == 1
    assert dr.exit_code([{"verdict": "control_failed"}, {"verdict": "finding"}]) == 2
    assert dr.exit_code([{"verdict": "control_ok"}]) == 0


def _main(tmp_path: Path, argv: list[str], world: World) -> int:
    return dr.main(
        [
            *argv,
            "--node",
            "TEST-1",
            "--port",
            "/dev/fake",
            "--out",
            str(tmp_path / "o.jsonl"),
            "--settle",
            "0.2",
        ],
        opener_factory=lambda port: world.opener,
        guard=lambda node, port: None,
        resolver=lambda a: "/dev/fake",
        clock=world.clock,
    )


def test_main_clean_run_exit_0_and_files(tmp_path: Path) -> None:
    w = World(FakeClock())
    rc = _main(tmp_path, ["--strategy", "hardreset", "--runs", "2"], w)
    assert rc == 0
    rows = [json.loads(x) for x in (tmp_path / "o.jsonl").read_text().splitlines()]
    assert len(rows) == 2
    r = rows[0]
    for key in (
        "strategy",
        "run",
        "steps",
        "lines",
        "events",
        "track_before",
        "track_after",
        "reset_seen",
        "rom_seen",
        "verdict",
    ):
        assert key in r
    assert r["strategy"] == "hardreset" and r["verdict"] == "ok" and r["track_before"] is False
    summary = (tmp_path / "o.summary").read_text()
    assert summary.startswith("hardreset runs=2 runs_with_events=0 runs_track_changed=0 verdict=ok")


def test_main_events_without_expectation_exit_1_and_track_restored(tmp_path: Path) -> None:
    w = World(FakeClock())
    real_setattr = FakeSerial.__setattr__

    def hook(self: FakeSerial, name: str, value: Any) -> None:
        real_setattr(self, name, value)
        if name == "dtr" and value is True and "world" in self.__dict__:
            self.world.script.append((self.clock.now, b"TripleClick\nTrack on - warning\n"))
            self.world.track = "on"

    FakeSerial.__setattr__ = hook  # type: ignore[method-assign]
    try:
        rc = _main(tmp_path, ["--strategy", "rts_then_dtr", "--runs", "1"], w)
    finally:
        FakeSerial.__setattr__ = real_setattr  # type: ignore[method-assign]
    assert rc == 1
    row = json.loads((tmp_path / "o.jsonl").read_text().splitlines()[0])
    assert row["verdict"] == "finding" and row["track_changed"] is True
    assert row["track_after"] is True and row["track_restored"] is True
    assert "--track off" in w.sent and w.track == "off"
    assert "verdict=finding" in (tmp_path / "o.summary").read_text()


def test_main_click_train_expect_events_exit_0(tmp_path: Path) -> None:
    w = World(FakeClock())
    real_setattr = FakeSerial.__setattr__

    def hook(self: FakeSerial, name: str, value: Any) -> None:
        real_setattr(self, name, value)
        if name == "dtr" and value is True and "world" in self.__dict__:
            self.world.script.append((self.clock.now + 1.0, b"TripleClick\n"))

    FakeSerial.__setattr__ = hook  # type: ignore[method-assign]
    try:
        rc = _main(tmp_path, ["--strategy", "click_train", "--runs", "1", "--expect-events"], w)
    finally:
        FakeSerial.__setattr__ = real_setattr  # type: ignore[method-assign]
    assert rc == 0
    assert "verdict=control_ok" in (tmp_path / "o.summary").read_text()


def test_main_control_without_events_is_harness_failure(tmp_path: Path) -> None:
    w = World(FakeClock())
    assert _main(tmp_path, ["--strategy", "click_train", "--runs", "1", "--expect-events"], w) == 2


def test_main_setup_sends_setcont(tmp_path: Path) -> None:
    w = World(FakeClock())
    _main(tmp_path, ["--strategy", "hardreset", "--runs", "1", "--setup"], w)
    assert "--setcont on" in w.sent


def test_reset_seen_waits_for_ready(tmp_path: Path) -> None:
    w = World(FakeClock())
    real_setattr = FakeSerial.__setattr__

    def hook(self: FakeSerial, name: str, value: Any) -> None:
        real_setattr(self, name, value)
        if name == "rts" and value is True and "world" in self.__dict__:
            self.world.script.append(
                (self.clock.now + 0.15, b"rst:0x1 (POWERON),boot:0x13\nRESET_REASON=1\n")
            )
            self.world.script.append((self.clock.now + 4.0, b"[BOOT];ready;ms;4000;ip;0\n"))

    FakeSerial.__setattr__ = hook  # type: ignore[method-assign]
    try:
        rc = _main(tmp_path, ["--strategy", "hardreset", "--runs", "1"], w)
    finally:
        FakeSerial.__setattr__ = real_setattr  # type: ignore[method-assign]
    assert rc == 0
    row = json.loads((tmp_path / "o.jsonl").read_text().splitlines()[0])
    assert row["reset_seen"] is True and row["verdict"] == "ok"
    assert any("[BOOT];ready" in x["text"] for x in row["lines"])


def test_harness_error_when_app_silent(tmp_path: Path) -> None:
    w = World(FakeClock())
    FakeSerial.write = lambda self, data: len(data)  # type: ignore[method-assign]
    rc = _main(tmp_path, ["--strategy", "hardreset", "--runs", "1"], w)
    assert rc == 2


@pytest.fixture(autouse=True)
def _restore_fake_write() -> Any:
    orig = FakeSerial.write
    yield
    FakeSerial.write = orig  # type: ignore[method-assign]
