"""Tests for tools/bench/gwrx_bench.py and gwrx_frame.py (no hardware, no network)."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

BENCH = Path(__file__).resolve().parents[1] / "bench"
sys.path.insert(0, str(BENCH))

import gwrx_bench as gb  # noqa: E402
import gwrx_frame  # noqa: E402

REPO = BENCH.parents[1]


def plan_for(*argv: str) -> gb.Plan:
    return gb.Plan(gb.build_parser().parse_args(list(argv)))


# ----------------------------------------------------------------------- frame


def test_selftest_matches_golden_f004(capsys):
    gwrx_frame.selftest()
    lines = (REPO / "test/golden/corpus/lora/04-f004.hex").read_text().splitlines()
    corpus = "".join(ln.strip() for ln in lines if ln.strip() and not ln.startswith("#"))
    assert corpus.upper() == gwrx_frame.GOLDEN_F004


def test_build_round_trips_fields():
    f = gwrx_frame.build(0x4D301230, 0xA2, "DK5EN-97", "9999", "hello")
    assert f[0] == 0x3A
    assert int.from_bytes(f[1:5], "little") == 0x4D301230
    assert f[5] == 0xA2
    assert f[-1] == 0x7E
    body_end = len(f) - 6  # fcs(2) + fw, last_hw, sub, 0x7E
    assert f[6 : body_end - 3] == b"DK5EN-97>9999:hello"
    assert int.from_bytes(f[body_end : body_end + 2], "big") == sum(f[:body_end]) & 0xFFFF


# ------------------------------------------------------------------------- CLI


def test_node_both_transports():
    p = plan_for(
        "--node",
        "DK5EN-1=tcp:somehost",
        "--node",
        "dk5en-90=serial:ABC123",
        "--mock-host",
        "10.0.0.1",
    )
    assert p.nodes == [("DK5EN-1", "tcp", "somehost"), ("DK5EN-90", "serial", "ABC123")]
    assert "--gateway on" in p.setup["DK5EN-1"]
    assert "DK5EN-90" in p.describe()


@pytest.mark.parametrize(
    "spec", ["DK5EN-1=udp:x", "DK5EN-1", "DK5EN-1=tcp:", "DK5EN-1=/dev/ttyUSB0", "XX0XXX=tcp:h"]
)
def test_bad_node_rejected(spec):
    with pytest.raises(gb.PlanError):
        plan_for("--node", spec, "--mock-host", "10.0.0.1")


def test_gateway_without_srvip_refused():
    with pytest.raises(gb.PlanError, match="srvip"):
        plan_for("--node", "DK5EN-1=tcp:h", "--setup-cmd", "DK5EN-1=--gateway on", "--frames", "0")
    with pytest.raises(gb.PlanError, match="srvip"):
        plan_for(
            "--node",
            "DK5EN-1=tcp:h",
            "--frames",
            "0",
            "--setup-cmd",
            "DK5EN-1=--gateway on",
            "--setup-cmd",
            "DK5EN-1=--srvip 10.0.0.1",
        )
    p = plan_for(
        "--node",
        "DK5EN-1=tcp:h",
        "--frames",
        "0",
        "--mock-host",
        "10.0.0.1",
        "--setup-cmd",
        "DK5EN-1=--srvip 10.0.0.1",
        "--setup-cmd",
        "DK5EN-1=--gateway on",
    )
    assert p.setup["DK5EN-1"] == ["--srvip 10.0.0.1", "--gateway on"]


@pytest.mark.parametrize("group", ["0", "000", "*", ""])
def test_bad_group_refused(group):
    with pytest.raises(gb.PlanError, match="group"):
        plan_for("--node", "DK5EN-1=tcp:h", "--mock-host", "10.0.0.1", "--group", group)


def test_help_exits_zero(capsys):
    with pytest.raises(SystemExit) as e:
        gb.main(["--help"])
    assert e.value.code == 0


# --------------------------------------------------------------------- summary

MID = 0x4D301230
H = f"{MID:08X}"


def test_tally_counts():
    lines = [
        f"12:00:01 [NBR]|NEED|136|{H}|T|B|00000002|00000000|1|00000020",
        f"12:00:02 [NBR]|NEED|136|{H}|T|A|00000002|00000001|1|00000020",
        f"12:00:03 [NBR]|NEED|136|{H}|T|U|00000000|00000000|1|00000020",
        f"12:00:04 [NBR]|CANCEL?|137|{H}|T|DK5EN-93|00000006|00000000|00000020",
        f"12:00:05 [NBR]|CANCEL|137|{H}|T|DK5EN-93|00000006|00000000|00000020",
        f"12:00:06 [NBR]|REFUSE|137|{H}|T|DK5EN-93|1",
        f"12:00:07 GWI x{H} len=40",
        f"12:00:08 TX x{H} len=40 src=g",
        "12:00:09 [NBR]|NEED|136|DEADBEEF|T|B|00000002|00000000|1|00000020",
    ]
    t = gb.tally(lines, [MID])[H]
    assert (t["A"], t["B"], t["U"]) == (1, 1, 1)
    assert (t["CANCEL?"], t["CANCEL"], t["REFUSE"]) == (1, 1, 1)
    assert t["GWI"] == 1 and t["TXg"] == 1
    assert "transmitted yes" in gb.format_summary("DK5EN-1", {H: t})


def test_tally_not_transmitted():
    t = gb.tally([f"[NBR]|NEED|1|{H}|T|B|0|0|1"], [MID])[H]
    assert t["TXg"] == 0
    assert "transmitted no" in gb.format_summary("X", {H: t})


# ---------------------------------------------------------------------- serial


def fake_ports(monkeypatch, serials):
    ports = [
        SimpleNamespace(device=f"/dev/fake{i}", serial_number=s) for i, s in enumerate(serials)
    ]
    monkeypatch.setattr(gb, "list_ports", SimpleNamespace(comports=lambda: ports))


def test_serial_port_match_by_serial_number(monkeypatch):
    fake_ports(monkeypatch, [None, "AAA", "230D6EBB3266D20E"])
    assert gb.resolve_serial_port("230d6ebb3266d20e") == "/dev/fake2"


def test_serial_port_no_match(monkeypatch):
    fake_ports(monkeypatch, ["AAA"])
    with pytest.raises(gb.PlanError, match="no USB serial port.*ZZZ.*AAA"):
        gb.resolve_serial_port("ZZZ")


class FakeTransport:
    def __init__(self):
        self.q = ["hello", "[SRVIP] set"]
        self.closed = False

    def connect(self): ...
    def send_line(self, line): ...

    def readline(self, timeout):
        if self.closed:
            raise EOFError
        return self.q.pop(0) if self.q else None

    def close(self):
        self.closed = True


def test_node_reader_exits_on_close_and_wait_for():
    n = gb.Node("DK5EN-1", FakeTransport())
    n.start()
    assert n.wait_for(r"\[SRVIP\].*set", 0, 2) == "[SRVIP] set"
    assert n.wait_for("never", 0, 0.2) is None
    n.close()
    assert n._thread is not None and not n._thread.is_alive()


def test_info_state_and_restore_keep_original_gateway():
    lines = ["... NBRRELAY on ...NBRDEBUG on", "MeshCom 4.40a / Gateway on "]
    st = gb.parse_info_state(lines)
    assert (st[0], st[1], st[2]) == ("on", "on", "on")
    on = gb.default_restore("on", "on")
    assert on.index("--gateway off") < on.index("--srvip 0.0.0.0") < on.index("--gateway on")
    assert "--gateway on" not in gb.default_restore("count", "off")


def test_info_state_missing_fields_are_none():
    st = gb.parse_info_state(["nothing"])
    assert (st[0], st[1], st[2]) == (None, None, None)
    st = gb.parse_info_state(["...NBRRELAY count", "...NBRDEBUG off"])
    assert (st[0], st[1], st[2]) == ("count", None, "off")


def test_default_restore_restores_nbrdebug_original():
    cmds = gb.default_restore("count", "off", "on")
    assert cmds[:2] == ["--gateway off", "--srvip 0.0.0.0"]
    assert "--nbrdebug on" in cmds and "--nbrdebug off" not in cmds
    assert "--nbrdebug off" in gb.default_restore("count", "off")


# ------------------------------------------------------------- plan safety checks


def _setup_plan(*cmds: str) -> gb.Plan:
    argv = ["--node", "DK5EN-1=tcp:h", "--frames", "0", "--mock-host", "10.0.0.1"]
    for c in cmds:
        argv += ["--setup-cmd", f"DK5EN-1={c}"]
    return plan_for(*argv)


@pytest.mark.parametrize(
    "cmds",
    [
        ("--srvip 10.0.0.1", "--srvip 0.0.0.0", "--gateway on"),
        ("--srvip mock.local", "--gateway on"),
        ("--srvip", "--gateway on"),
        ("--srvip 999.1.1.1", "--gateway on"),
    ],
)
def test_srvip_must_be_real_ipv4_and_still_set(cmds):
    with pytest.raises(gb.PlanError, match="srvip"):
        _setup_plan(*cmds)


def test_srvip_reset_then_set_again_is_fine():
    p = _setup_plan("--srvip 0.0.0.0", "--srvip 10.0.0.1", "--gateway on")
    assert p.setup["DK5EN-1"][-1] == "--gateway on"


@pytest.mark.parametrize("call", ["OE1XAR-1", "XX0XXX", "DK5EN", "DK5EN-", "DK5EN-1x", "DL1ABC-9"])
def test_foreign_source_call_refused(call):
    with pytest.raises(gb.PlanError, match="source"):
        plan_for("--node", "DK5EN-1=tcp:h", "--mock-host", "10.0.0.1", "--source-call", call)


@pytest.mark.parametrize("group", ["9", "9999", "TEST", "DK5EN-12"])
def test_allowed_groups(group):
    p = plan_for("--node", "DK5EN-1=tcp:h", "--mock-host", "10.0.0.1", "--group", group)
    assert p.args.group == group


@pytest.mark.parametrize("group", ["10", "262", "OE1XAR-1", "DK5EN", "ALL", "DL1ABC-9"])
def test_foreign_group_refused(group):
    with pytest.raises(gb.PlanError, match="group"):
        plan_for("--node", "DK5EN-1=tcp:h", "--mock-host", "10.0.0.1", "--group", group)


@pytest.mark.parametrize("flags", ["22", "A", "7F", "00", "1FF", "zz"])
def test_flags_without_server_bit_refused(flags):
    with pytest.raises(gb.PlanError, match="flags"):
        plan_for("--node", "DK5EN-1=tcp:h", "--mock-host", "10.0.0.1", "--flags", flags)


@pytest.mark.parametrize("flags", ["A2", "0x80", "FF"])
def test_flags_with_server_bit_accepted(flags):
    p = plan_for("--node", "DK5EN-1=tcp:h", "--mock-host", "10.0.0.1", "--flags", flags)
    assert p.flags & 0x80


# ------------------------------------------------------- run() with a fake transport

import queue  # noqa: E402
import threading  # noqa: E402
import time  # noqa: E402

INFO_LINES = [
    "...NBRRELAY off ...relays A 0 B 0",
    "...DEBUG off ...LORADEBUG off ...NBRDEBUG on ...TXCAPTURE off",
    " / Gateway off ",
]


class FakeLink:
    """Scripted console: replies to --info / --srvip, can fail, drop, or 'reboot'."""

    def __init__(
        self,
        *,
        info=None,
        srvip="ok",
        fail=(),
        fail_once=(),
        boot_on=None,
        reconnect_ok=False,
        connect_fails=False,
        lose_once=(),
        kbint_on=None,
        order=None,
        name="",
    ):
        self.info = INFO_LINES if info is None else info
        self.srvip = srvip
        self.fail, self.fail_once = set(fail), set(fail_once)
        self.boot_on, self.reconnect_ok, self.connect_fails = boot_on, reconnect_ok, connect_fails
        self.q: queue.Queue[str] = queue.Queue()
        self.sent: list[str] = []
        self.closed = False
        self.reconnects = 0
        self.lose_once, self.kbint_on = set(lose_once), kbint_on
        self.order, self.name = order, name
        self.gateway = "on" if any("Gateway on" in ln for ln in self.info) else "off"
        m = next((re.search(r"NBRRELAY (\w+)", ln) for ln in self.info if "NBRRELAY" in ln), None)
        self.nbrrelay = m.group(1) if m else None

    def _info(self):
        out = []
        for ln in self.info:
            ln = re.sub(r"Gateway (on|off)", f"Gateway {self.gateway}", ln)
            if self.nbrrelay:
                ln = re.sub(r"NBRRELAY \w+", f"NBRRELAY {self.nbrrelay}", ln)
            out.append(ln)
        return out

    def connect(self):
        if self.connect_fails:
            raise ConnectionError("no route")

    def reconnect(self):
        self.reconnects += 1
        if not self.reconnect_ok:
            raise ConnectionError("still down")
        self.closed = False

    def send_line(self, line):
        if line == self.kbint_on:
            self.kbint_on = None  # once: a real Ctrl-C is ignored during the restore
            raise KeyboardInterrupt
        if line in self.fail:
            raise RuntimeError("boom")
        if self.closed:
            raise OSError("connection reset")  # the RST after a lost send
        if line in self.lose_once:  # sendall succeeds, the peer never sees it, then RST
            self.lose_once.discard(line)
            self.closed = True
            return
        if line in self.fail_once:
            self.fail_once.discard(line)
            self.closed = True
            raise OSError("link dropped")
        self.sent.append(line)
        if self.order is not None:
            self.order.append((self.name, line))
        words = line.lower().split()
        if words[:1] == ["--gateway"] and words[1:2] in (["on"], ["off"]):
            self.gateway = words[1]
        if words[:1] == ["--nbrrelay"] and len(words) > 1:
            self.nbrrelay = words[1]
        if line == "--info":
            for ln in self._info():
                self.q.put(ln)
        elif line.startswith("--srvip "):
            ip = line.split()[1]
            if ip == "0.0.0.0":
                self.q.put("[SRVIP];0.0.0.0;set")
            elif self.srvip != "none":
                self.q.put(f"[SRVIP];{ip if self.srvip == 'ok' else '10.9.9.9'};set")
        if self.boot_on == line:
            threading.Timer(0.1, lambda: self.q.put("[BOOT];ready;1234;0;0.0.0.0;0")).start()

    def readline(self, timeout):
        try:
            return self.q.get(timeout=min(timeout, 0.2))
        except queue.Empty:
            if self.closed:
                raise EOFError("closed") from None
            return None

    def close(self):
        self.closed = True


class FakeMock:
    sent: list[bytes] = []

    def __init__(self, *a, **k): ...
    def send_keep(self): ...
    def send_data(self, frame):
        FakeMock.sent.append(frame)

    def close(self): ...


@pytest.fixture(autouse=True)
def fast(monkeypatch):
    monkeypatch.setattr(gb, "CMD_WAIT_SCALE", 0.03, raising=False)
    monkeypatch.setattr(gb, "SRVIP_TIMEOUT", 0.3, raising=False)
    monkeypatch.setattr(gb, "REBOOT_SETTLE", 0.0, raising=False)
    monkeypatch.setattr(gb, "LINK_LOST_RECONNECT_S", 0.3, raising=False)
    monkeypatch.setattr(gb, "LINK_LOST_RETRY_S", 0.01, raising=False)
    monkeypatch.setattr(gb, "TAIL_WAIT_S", 0.0, raising=False)
    FakeMock.sent = []
    monkeypatch.setitem(sys.modules, "mock_client", SimpleNamespace(MockNode=FakeMock))


def run_links(tmp_path, links, *extra, frames=0, warmup=30):
    argv = []
    for call in links:
        argv += ["--node", f"{call}=tcp:h"]
    argv += [
        "--mock-host",
        "10.0.0.1",
        "--frames",
        str(frames),
        "--warmup",
        str(warmup),
        "--interval",
        "1",
        "--log-dir",
        str(tmp_path),
        *extra,
    ]
    plan = plan_for(*argv)
    nodes = [gb.Node(c, link) for c, link in links.items()]
    return gb.run(plan, nodes, "t")


SAFETY = ["--gateway off", "--srvip 0.0.0.0"]
SAFETY_SENT = [*SAFETY, "--info"]  # --info confirms "Gateway off" before the tail
FULL_RESTORE = [
    *SAFETY_SENT,
    "--nbrdebug on",
    "--nbrrelay off",
    "--info",
]  # last --info = final check
FULL_SETUP = ["--srvip 10.0.0.1", "--nbrrelay count", "--nbrdebug on", "--gateway on"]


def test_happy_path_order(tmp_path):
    link = FakeLink()
    assert run_links(tmp_path, {"DK5EN-1": link}) == 0
    assert link.sent == ["--info", *FULL_SETUP, *FULL_RESTORE]


@pytest.mark.parametrize("mode", ["none", "wrong"])
def test_no_or_wrong_srvip_confirmation_never_enables_gateway(tmp_path, mode):
    link = FakeLink(srvip=mode)
    assert run_links(tmp_path, {"DK5EN-1": link}) != 0
    assert "--gateway on" not in link.sent
    assert link.sent == ["--info", "--srvip 10.0.0.1", *FULL_RESTORE]


def test_setup_failure_on_node2_restores_began_nodes_only(tmp_path):
    l1, l2, l3 = FakeLink(), FakeLink(srvip="none"), FakeLink()
    rc = run_links(tmp_path, {"DK5EN-1": l1, "DK5EN-2": l2, "DK5EN-3": l3})
    assert rc != 0
    assert l1.sent == ["--info", *FULL_SETUP, *FULL_RESTORE]
    assert l2.sent == ["--info", "--srvip 10.0.0.1", *FULL_RESTORE]
    assert l3.sent == ["--info"]


def test_connect_failure_means_no_setup_and_no_restore(tmp_path):
    l1, l2, l3 = FakeLink(), FakeLink(connect_fails=True), FakeLink()
    rc = run_links(tmp_path, {"DK5EN-1": l1, "DK5EN-2": l2, "DK5EN-3": l3})
    assert rc != 0
    assert l1.sent == ["--info"]
    assert l2.sent == [] and l3.sent == []


def test_restore_exception_does_not_stop_other_nodes(tmp_path, capsys):
    l1 = FakeLink(fail={"--srvip 0.0.0.0"})
    l2 = FakeLink()
    rc = run_links(tmp_path, {"DK5EN-1": l1, "DK5EN-2": l2})
    assert rc == 3
    assert l2.sent == ["--info", *FULL_SETUP, *FULL_RESTORE]
    assert "--gateway off" in l1.sent
    err = capsys.readouterr().err
    assert "NODE LEFT IN BENCH STATE: DK5EN-1" in err
    assert "--gateway off / --srvip 0.0.0.0" in err
    assert "DK5EN-2" not in err


def test_dropped_link_in_restore_reconnects_and_resends(tmp_path):
    link = FakeLink(fail_once={"--srvip 0.0.0.0"}, reconnect_ok=True)
    assert run_links(tmp_path, {"DK5EN-1": link}) == 0
    # one reconnect at the failed send; FakeLink's shared queue can eat the reply on the stale
    # reader, then the safety steps are redone on a second fresh link -- both end confirmed
    assert link.reconnects in (1, 2)
    assert link.sent[-len(FULL_RESTORE) :] == FULL_RESTORE
    assert link.gateway == "off"


def test_reboot_marker_sends_gateway_off_at_once_and_aborts(tmp_path):
    link = FakeLink(boot_on="--gateway on")
    t0 = time.monotonic()
    rc = run_links(tmp_path, {"DK5EN-1": link}, frames=1, warmup=30)
    assert rc != 0
    assert time.monotonic() - t0 < 15, "run must abort, not sit through the 30 s warm-up"
    i = link.sent.index("--gateway on")
    assert link.sent[i + 1] == "--gateway off"  # emergency, before restore
    assert link.sent[i + 2 :] == FULL_RESTORE
    assert "--neighbours" not in link.sent and FakeMock.sent == []


@pytest.mark.parametrize("missing", ["NBRRELAY", "Gateway"])
def test_missing_info_line_aborts_before_setup(tmp_path, missing):
    info = [ln for ln in INFO_LINES if missing not in ln]
    link = FakeLink(info=info)
    assert run_links(tmp_path, {"DK5EN-1": link}) != 0
    assert link.sent == ["--info"]


def test_missing_nbrdebug_line_restores_off(tmp_path):
    info = [ln.replace("...NBRDEBUG on", "") for ln in INFO_LINES]
    link = FakeLink(info=info)
    assert run_links(tmp_path, {"DK5EN-1": link}) == 0
    assert link.sent[-3:] == ["--nbrdebug off", "--nbrrelay off", "--info"]


def test_nbrdebug_original_off_is_restored_off(tmp_path):
    info = [ln.replace("NBRDEBUG on", "NBRDEBUG off") for ln in INFO_LINES]
    link = FakeLink(info=info)
    assert run_links(tmp_path, {"DK5EN-1": link}) == 0
    assert link.sent[-3:] == ["--nbrdebug off", "--nbrrelay off", "--info"]


def test_custom_restore_does_not_replace_safety_steps(tmp_path):
    link = FakeLink()
    rc = run_links(tmp_path, {"DK5EN-1": link}, "--restore-cmd", "DK5EN-1=--nbrdebug off")
    assert rc == 0
    assert link.sent[-5:] == [*SAFETY_SENT, "--nbrdebug off", "--info"]


# ---------------------------------------------------------------------- serial port


def test_serial_port_opened_exclusive(monkeypatch):
    seen: dict[str, object] = {}

    class FakeSerial:
        def __init__(self):
            self.exclusive = None
            self.dtr = None

        def open(self):
            seen["exclusive_at_open"] = self.exclusive

    monkeypatch.setattr(gb, "serial", SimpleNamespace(Serial=FakeSerial))
    monkeypatch.setattr(gb, "resolve_serial_port", lambda sn: "/dev/fake")
    gb.SerialTransport("ABC").connect()
    assert seen["exclusive_at_open"] is True


class DroppingLink(FakeLink):
    """2323 reboot: the socket drops after --gateway on, no banner; reconnect works after a while."""

    def __init__(self, ok_after: int, reconnect_delay: float = 0.0, **kw):
        super().__init__(**kw)
        self.ok_after, self.reconnect_delay = ok_after, reconnect_delay

    def send_line(self, line):
        if self.closed:
            raise OSError("link dropped")
        super().send_line(line)
        if line == "--gateway on" and "--gateway off" not in self.sent:
            self.closed = True

    def reconnect(self):
        time.sleep(self.reconnect_delay)
        self.reconnects += 1
        if self.reconnects < self.ok_after:
            raise ConnectionError("still booting")
        self.closed = False


def test_tcp_link_drop_counts_as_reboot_and_gateway_goes_off(tmp_path, monkeypatch):
    monkeypatch.setattr(gb, "LINK_LOST_RETRY_S", 0.01)
    monkeypatch.setattr(gb, "LINK_LOST_RECONNECT_S", 5.0)
    # reconnects slower than the restore would start: without the emergency_idle wait the
    # restore's own sends would interleave with (or precede) the emergency --gateway off
    link = DroppingLink(ok_after=3, reconnect_delay=0.2)
    rc = run_links(tmp_path, {"DK5EN-1": link}, frames=1, warmup=30)
    assert rc != 0
    i = link.sent.index("--gateway on")
    assert link.sent[i + 1] == "--gateway off"  # emergency, right after the reconnect
    assert link.sent[i + 2 :] == FULL_RESTORE  # restore waited for the emergency path
    assert link.reconnects == 3


def test_tcp_link_never_back_reports_node_left(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(gb, "LINK_LOST_RETRY_S", 0.01)
    monkeypatch.setattr(gb, "LINK_LOST_RECONNECT_S", 0.1)
    link = DroppingLink(ok_after=10**9)
    rc = run_links(tmp_path, {"DK5EN-1": link}, frames=1, warmup=30)
    assert rc == 3
    assert "NODE LEFT IN BENCH STATE" in capsys.readouterr().err


# ----------------------------------------------------------- second advisor round


def test_safety_steps_reach_every_node_before_any_tail(tmp_path):
    order: list = []
    links = {c: FakeLink(order=order, name=c) for c in ("DK5EN-1", "DK5EN-2")}
    assert run_links(tmp_path, links) == 0
    restore = order[order.index(("DK5EN-1", "--gateway off")) :]
    last_safety = max(i for i, (_, ln) in enumerate(restore) if ln in SAFETY)
    first_tail = min(i for i, (_, ln) in enumerate(restore) if ln.startswith("--nbr"))
    assert last_safety < first_tail


def test_tail_failure_is_per_command(tmp_path):
    l1 = FakeLink(fail={"--nbrrelay off"}, reconnect_ok=True)
    rc = run_links(tmp_path, {"DK5EN-1": l1})
    assert rc == 3  # tail incomplete
    tail = l1.sent[l1.sent.index("--srvip 0.0.0.0") + 1 :]
    assert tail == ["--info", "--nbrdebug on", "--info"]  # a failed command did not stop the rest


def test_keyboard_interrupt_still_restores(tmp_path):
    link = FakeLink(kbint_on="--nbrdebug on")
    rc = run_links(tmp_path, {"DK5EN-1": link})
    assert rc == 130
    assert link.sent[-len(FULL_RESTORE) :] == FULL_RESTORE


def test_lost_gateway_off_is_redone_after_reconnect(tmp_path):
    link = FakeLink(lose_once={"--gateway off"}, reconnect_ok=True)
    rc = run_links(tmp_path, {"DK5EN-1": link})
    assert rc == 0
    assert link.gateway == "off"
    i = link.sent.index("--gateway off")  # the lost one never reached the node
    assert link.sent[i + 1] == "--srvip 0.0.0.0"
    assert link.sent[-1] == "--info"


def test_lost_gateway_off_on_gateway_node(tmp_path):
    info = [ln.replace("Gateway off", "Gateway on") for ln in INFO_LINES]
    link = FakeLink(info=info, lose_once={"--gateway off"}, reconnect_ok=True)
    rc = run_links(tmp_path, {"DK5EN-1": link})
    assert rc == 0
    i = link.sent.index("--gateway off")
    assert link.sent[i + 1] == "--srvip 0.0.0.0"
    assert link.sent[-2:] == ["--gateway on", "--info"]


def test_unconfirmed_final_state_is_node_left(tmp_path, capsys):
    class StuckGateway(FakeLink):
        def send_line(self, line):
            super().send_line(line)
            if line.startswith("--gateway"):
                self.gateway = "on"  # the node ignores --gateway off

    link = StuckGateway()
    assert run_links(tmp_path, {"DK5EN-1": link}) == 3
    assert "NODE LEFT IN BENCH STATE: DK5EN-1" in capsys.readouterr().err


@pytest.mark.parametrize("cmd", ["--GATEWAY ON", "--Gateway On"])
def test_uppercase_gateway_needs_srvip(cmd):
    with pytest.raises(gb.PlanError, match="srvip"):
        _setup_plan(cmd)


def test_uppercase_gateway_arms_reboot_guard(tmp_path):
    link = FakeLink(boot_on="--GATEWAY ON")
    rc = run_links(
        tmp_path,
        {"DK5EN-1": link},
        "--setup-cmd",
        "DK5EN-1=--srvip 10.0.0.1",
        "--setup-cmd",
        "DK5EN-1=--GATEWAY ON",
        frames=1,
        warmup=30,
    )
    assert rc == 4
    i = link.sent.index("--GATEWAY ON")
    assert link.sent[i + 1] == "--gateway off"


def test_srvip_must_be_the_mock_host():
    with pytest.raises(gb.PlanError, match="mock-host"):
        _setup_plan("--srvip 192.0.2.7", "--gateway on")


@pytest.mark.parametrize("cmd", ["--reboot", "--gateway srv OE", "--REBOOT"])
def test_forbidden_setup_commands(cmd):
    with pytest.raises(gb.PlanError, match="not allowed"):
        _setup_plan("--srvip 10.0.0.1", cmd)


@pytest.mark.parametrize("cmd", ["--gateway on", "--srvip 10.0.0.1", "--reboot"])
def test_forbidden_restore_commands(cmd):
    with pytest.raises(gb.PlanError):
        plan_for(
            "--node",
            "DK5EN-1=tcp:h",
            "--frames",
            "0",
            "--mock-host",
            "10.0.0.1",
            "--restore-cmd",
            f"DK5EN-1={cmd}",
        )


def test_frames_happy_path_writes_summary(tmp_path):
    link = FakeLink()
    rc = run_links(tmp_path, {"DK5EN-1": link}, frames=2, warmup=0)
    assert rc == 0
    assert len(FakeMock.sent) == 2
    assert list(tmp_path.glob("gwrx-*.summary"))


def test_srvip_after_gateway_on_must_stay_mock():
    for late in ("--srvip 0.0.0.0", "--srvip 192.0.2.9"):
        with pytest.raises(gb.PlanError, match="armed gateway"):
            _setup_plan("--srvip 10.0.0.1", "--gateway on", late)
    _setup_plan("--srvip 10.0.0.1", "--gateway on", "--srvip 10.0.0.1")


def test_reboot_during_restore_redoes_safety(tmp_path):
    class RebootInRestore(FakeLink):
        """Serial-like: the first --gateway off of the restore is lost in a reboot."""

        def __init__(self, **kw):
            super().__init__(**kw)
            self.restoring = False

        def send_line(self, line):
            if line == "--gateway off" and not self.restoring:
                self.restoring = True
                self.q.put("[BOOT];ready;1234;0;0.0.0.0;0")
                return  # lost in the boot window; gateway stays on (persisted)
            super().send_line(line)

    link = RebootInRestore()
    assert run_links(tmp_path, {"DK5EN-1": link}) == 0
    assert link.gateway == "off"
    assert link.sent[-1] == "--info"
