#!/usr/bin/env python3
"""Reboot guard + parsers for gwrun.py. Offline-testable: `python3 gwguard.py`.

Why: `--gateway on` is persisted, `--srvip` is RAM only. A reboot with the gateway on connects the node to the REAL server.
Boot markers (verified in source): ESP32 `[BOOT] RESET_REASON=%d` (src/esp32/esp32_main.cpp:1094; the net console may show it as
`...BOOT RESET_REASON=`), nRF52 `[BOOT];loopstack;words;N` / `;FAILED;` (src/main.cpp:69/74). On the RAK the boot text can be lost
while USB re-enumerates, so a `## dropped` line of its serial logger also counts.
"""
import re, threading, time, urllib.parse
from typing import Callable, Dict, List, Optional

BOOT_RX = re.compile(r"(?:\[BOOT\]|\.\.\.BOOT)\s+RESET_REASON=|\[BOOT\];loopstack;")
DROP_RX = re.compile(r"## dropped")
RAK_CALL = "DK5EN-90"
GW_RX = re.compile(r"Gateway\s+(on|off)\b")
CALL_RX = re.compile(r"Call:\s*<([^>]+)>")
KEEP_RX = re.compile(r"event=keep addr=(\S+):(\d+) gw=(\S+) callsign=(\S+)")
GW_OFF_PATH = "/setparam/?manualcommand=" + urllib.parse.quote("--gateway off", safe="")


def parse_info(text: str) -> Dict[str, Optional[str]]:
    """`--info` lines (command_functions.cpp:6793 `/ Gateway on|off`, `...Call: <X>`); last match wins."""
    g, c = GW_RX.findall(text), CALL_RX.findall(text)
    return {"gateway": g[-1] if g else None, "call": c[-1] if c else None}


def keep_seen(mock_log: str, call: str, host: str) -> bool:
    return any(m.group(4) == call and m.group(1) == host for m in KEEP_RX.finditer(mock_log))


def gateway_off_sender(node, http_fn: Callable, con_fn: Callable, tries: int = 120, sleep: float = 1.0, sleep_fn=time.sleep) -> bool:
    """Console/serial cmdfile first (works with no network), then web every `sleep` s until HTTP answers (max `tries`)."""
    con_fn(node)
    for i in range(tries):
        try:
            http_fn(node.base, GW_OFF_PATH)
            return True
        except Exception:  # noqa: BLE001  (no answer yet: node still booting)
            if i % 10 == 9:
                con_fn(node)
            sleep_fn(sleep)
    return False


class BootGuard(threading.Thread):
    """Tails each node log from the current end; on the first boot marker per node it launches `sender(node)` and sets `tripped`."""

    def __init__(self, nodes: List, sender: Callable, poll: float = 0.3) -> None:
        super().__init__(daemon=True)
        self.nodes, self.sender, self.poll = nodes, sender, poll
        self.tripped: Dict[str, float] = {}
        self.sent: Dict[str, bool] = {}
        self.stop_ev = threading.Event()
        self.off = {n.call: n.pos() for n in nodes}
        self.threads: List[threading.Thread] = []

    def scan(self) -> None:
        for n in self.nodes:
            if n.call in self.tripped:
                continue
            txt = n.text(self.off[n.call])
            m = BOOT_RX.search(txt) or (DROP_RX.search(txt) if n.call == RAK_CALL else None)
            if m:
                self.tripped[n.call] = time.time()
                print("BOOT GUARD: boot marker on %s (%r) -> gateway off" % (n.call, m.group(0)), flush=True)
                t = threading.Thread(target=lambda n=n: self.sent.__setitem__(n.call, bool(self.sender(n))), daemon=True)
                t.start(); self.threads.append(t)

    def run(self) -> None:
        while not self.stop_ev.is_set():
            self.scan(); time.sleep(self.poll)

    def join_senders(self, timeout: float = 130) -> None:
        for t in self.threads:
            t.join(timeout)


def _selftest() -> None:
    import os, tempfile
    class N:
        def __init__(self, call, log): self.call, self.log, self.base = call, log, "http://x"
        def pos(self): return os.path.getsize(self.log)
        def text(self, since=0):
            with open(self.log, errors="replace") as f: f.seek(since); return f.read()
    d = tempfile.mkdtemp(); l1, l2 = os.path.join(d, "1.log"), os.path.join(d, "2.log")
    open(l1, "w").write("[BOOT] RESET_REASON=1 old\n"); open(l2, "w").write("x\n")
    n1, n2 = N("DK5EN-1", l1), N("DK5EN-90", l2)
    calls: List = []; cons: List = []; fails = [3]
    def http_fn(base, path):
        calls.append(path)
        if fails[0] > 0:
            fails[0] -= 1; raise OSError("down")
    g = BootGuard([n1, n2], lambda n: gateway_off_sender(n, http_fn, lambda x: cons.append(x.call), sleep=0.01), poll=0.05)
    g.start(); time.sleep(0.2)
    assert not g.tripped, "old marker before start must be ignored"
    open(l1, "a").write("12:00:01 ## dropped: OSError\n"); time.sleep(0.3)
    assert not g.tripped, "console drop alone must not trip the Heltec"
    open(l2, "a").write("12:00:02 ## dropped: OSError\n")
    for _ in range(100):
        if g.sent: break
        time.sleep(0.05)
    assert list(g.tripped) == ["DK5EN-90"] and g.sent == {"DK5EN-90": True}, (g.tripped, g.sent)
    assert calls == [GW_OFF_PATH] * 4 and cons[0] == "DK5EN-90", (calls, cons)
    assert GW_OFF_PATH == "/setparam/?manualcommand=--gateway%20off"
    open(l1, "a").write("20:25:13 ...BOOT RESET_REASON=3 SW\n"); time.sleep(0.4)
    assert "DK5EN-1" in g.tripped
    g.stop_ev.set(); g.join_senders(2)
    assert BOOT_RX.search("12:00:00 [BOOT];loopstack;words;1024")
    assert parse_info("...Call: <DK5EN-1> ...ID 4\n...Webserver  on / Webpwd <> / Gateway off \n") == {"gateway": "off", "call": "DK5EN-1"}
    k = "2026-10-06 10:00:00,1 mock event=keep addr=192.168.68.71:1990 gw=433A8968 callsign=DK5EN-1 ver=4.40 groups=TEST"
    assert keep_seen(k, "DK5EN-1", "192.168.68.71") and not keep_seen(k, "DK5EN-90", "192.168.68.73") and not keep_seen(k, "DK5EN-1", "192.168.68.73")
    print("gwguard selftest OK")


if __name__ == "__main__":
    _selftest()
