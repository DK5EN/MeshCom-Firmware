"""GWACK-01 bench driver: how a gateway's two 0x41 ACKs look on air.

A sender node posts group texts over its console; every gateway ACKs each heard text twice
(`rx_dm_ack_gw` + `rx_dm_ack_new`, src/lora_functions.cpp OnRxDone). The driver logs every
console and reports per gateway whether the two ACK copies carry the same or distinct msg ids,
plus what listeners and an optional relay made of the copies.

Phases:
  timing     send --count texts; gateways run with --info-mode on|off
  missfirst  per text: --listener goes --airgap on before the send and --airgap off once the
             --watch gateway's first ACK copy is over the air; does it take the second copy?

Gateways are moved to the mock server for the run (--gateway off, --srvip <mock>, --gateway on;
INSTRUMENT image needed for --srvip) and put back in a finally block: --gateway off,
--srvip 0.0.0.0, then the gateway / DisplayInfo / MESH state read from --info at the start.
A reboot of any node aborts the sends (the RAM-only srvip is gone, so a rebooted gateway is
on the real server again). Group texts only (9 / 9999); never broadcast.

Example:
  python3 tools/bench/gwack_bench.py --phase timing --info-mode off \\
      --gw DK5EN-1=tcp:192.168.68.73 --gw DK5EN-90=serial:230D6EBB3266D20E \\
      --sender DK5EN-14=tcp:192.168.68.66 --relay DK5EN-14 --mock-host 192.168.68.58

Passwords come from .env (MC_PW_<CALL>), never from the command line.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import threading
import time
from pathlib import Path
from typing import Optional

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools"))

import gwrx_bench as gb  # noqa: E402

ALLOWED_GROUPS = ("9", "9999")
OWN_TX_RE = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) A H([0-9A-F]{2}) .*?wait=(\d+).*?t=(\d+)")
RX_ACK_RE = re.compile(
    r"\[LOG\] 012 A x([0-9A-F]{8}) H([0-9A-F]{2}) x([0-9A-F]{8}) .*?DUP:([nd]).*?t=(\d+)"
)


PAIR_MAX_ID_DELTA = 200  # ms: the two millis() reads of one gateway lie closer than this
PAIR_MAX_GAP_MS = 60000  # the second copy leaves the TX ring within a minute (field max 34 s)


def own_ack_tx(lines: list[str]) -> list[tuple[str, int]]:
    """(msg id, t ms) of every 0x41 the node sent at full hop (its own ACKs, not relays)."""
    out = []
    for m in map(OWN_TX_RE.search, lines):
        if m and m.group(2) in ("84", "04"):
            out.append((m.group(1), int(m.group(4))))
    return out


def pair_acks(own: list[tuple[str, int]]) -> list[dict]:
    """Pair consecutive own ACK copies by id, not by time window: the sender's TX queue can
    reorder texts, so a window cut splits pairs. Same id, or ids at most PAIR_MAX_ID_DELTA
    apart (two millis() reads), sent within PAIR_MAX_GAP_MS, form one pair."""
    rows, i = [], 0
    while i < len(own):
        a = own[i]
        if i + 1 < len(own):
            b = own[i + 1]
            if (abs(int(b[0], 16) - int(a[0], 16)) <= PAIR_MAX_ID_DELTA
                    and 0 <= b[1] - a[1] < PAIR_MAX_GAP_MS):
                rows.append({"ids": [a[0], b[0]], "kind": "same" if a[0] == b[0] else "distinct",
                             "id_delta": abs(int(b[0], 16) - int(a[0], 16)), "gap_ms": b[1] - a[1]})
                i += 2
                continue
        rows.append({"ids": [a[0]], "kind": "single"})
        i += 1
    return rows


def heard_acks(lines: list[str], ids: set) -> dict[str, list[str]]:
    """Per own ACK id: every copy a node heard, as '<hop byte><dup flag>' (e.g. '84n', '83d')."""
    seen: dict[str, list[str]] = {}
    for m in map(RX_ACK_RE.search, lines):
        if m and m.group(1) in ids:
            seen.setdefault(m.group(1), []).append(f"{m.group(2)}{m.group(4)}")
    return seen


def listener_tally(pairs: dict[str, list[dict]], heard: dict[str, dict], call: str) -> dict:
    """For every pair of the other gateways: direct (H84) copies this node heard, with flags."""
    tally: dict[str, int] = {}
    for gw, rows in pairs.items():
        if gw == call:
            continue
        for r in rows:
            if r["kind"] == "single":
                continue
            direct = [f for i in dict.fromkeys(r["ids"]) for f in heard.get(i, []) if f.startswith("84")]
            key = f"{r['kind']}:direct{len(direct)}:{''.join(f[-1] for f in direct)}"
            tally[key] = tally.get(key, 0) + 1
    return tally


def relay_per_pair(pairs: dict[str, list[dict]], heard: dict[str, dict]) -> dict[str, int]:
    """'<kind>:<n>': pairs of which n distinct ids were heard one hop down (H83) anywhere. A relay
    forwards an id once, so n is the relay TX count per pair of a single relay in range."""
    out: dict[str, int] = {}
    for rows in pairs.values():
        for r in rows:
            if r["kind"] == "single":
                continue
            n = sum(1 for i in set(r["ids"])
                    if any(f.startswith("83") for h in heard.values() for f in h.get(i, [])))
            key = f"{r['kind']}:{n}"
            out[key] = out.get(key, 0) + 1
    return out


def info_state(node: gb.Node) -> dict:
    s = len(node.lines)
    node.cmd("--info", 3)
    st: dict = {}
    for ln in node.lines[s:]:
        for key, pat in (
            ("gateway", r"/ Gateway (on|off)"),
            ("info", r"DisplayInfo (on|off)"),
            ("mesh", r"\.\.\.MESH (on|off)"),
            ("server", r"Server\s*: (.*)$"),
        ):
            m = re.search(pat, ln)
            if m and key not in st:
                st[key] = m.group(1).strip()
    return st


class Bench:
    def __init__(self, a: argparse.Namespace) -> None:
        self.a = a
        self.stamp = time.strftime("%Y%m%d-%H%M%S")
        self.nodes: dict[str, gb.Node] = {}
        self.roles: dict[str, set] = {}
        self.state: dict[str, dict] = {}
        self.abort = threading.Event()
        self.reasons: list[str] = []
        self.touched: list[str] = []
        self.gws = [gb.parse_node(s) for s in a.gw]
        self.sender = gb.parse_node(a.sender)
        self.listener = gb.parse_node(a.listener) if a.listener else None

    def add(self, spec: tuple[str, str, str], role: str) -> gb.Node:
        call, kind, value = spec
        if call not in self.nodes:
            log = self.a.log_dir / f"gwack-{self.a.tag}-{call.lower()}-{self.stamp}.log"
            self.nodes[call] = gb.Node(call, gb.make_transport(kind, value, call), log)
            self.roles[call] = set()
        self.roles[call].add(role)
        return self.nodes[call]

    def on_reboot(self, n: gb.Node, line: str) -> None:
        msg = f"{n.call}: REBOOT detected ({line.strip()!r})"
        self.reasons.append(msg)
        print(msg)
        self.abort.set()

    def wait(self, secs: float) -> None:
        if self.abort.wait(secs):
            raise gb.RunAborted("; ".join(self.reasons))

    def setup(self) -> None:
        for spec in self.gws:
            self.add(spec, "gw")
        self.add(self.sender, "sender")
        if self.listener:
            self.add(self.listener, "listener")
        for n in self.nodes.values():
            n.abort_event, n.on_reboot = self.abort, self.on_reboot
            n.start()
            self.state[n.call] = info_state(n)
            print(f"{n.call}: start state {self.state[n.call]}")
            if "gateway" not in self.state[n.call]:
                raise gb.SetupError(f"{n.call}: --info has no Gateway line")
        if self.state[self.sender[0]]["gateway"] == "on":
            raise gb.SetupError("sender is a gateway: its texts carry the server bit, no gateway ACKs them")
        for n in self.nodes.values():
            n.watch_boot = True
        for call, n in self.nodes.items():
            self.touched.append(call)
            if "gw" in self.roles[call]:
                n.cmd("--gateway off", 3)
                s = len(n.lines)
                n.cmd(f"--srvip {self.a.mock_host}", 2)
                if not n.wait_for(rf"\[SRVIP\];{re.escape(self.a.mock_host)};set", s, gb.SRVIP_TIMEOUT):
                    raise gb.SetupError(f"{call}: no [SRVIP] confirmation (INSTRUMENT image?)")
                n.cmd("--gateway on", 3)
                n.cmd(f"--setinfo {self.a.info_mode}", 1.5)
            if call == self.a.relay:
                n.cmd("--mesh on", 1.5)
            if "listener" in self.roles[call]:
                s = len(n.lines)
                n.cmd("--airgap off", 1.5)
                if not n.wait_for(r"\[AIRGAP\];off", s, 5):
                    raise gb.SetupError(f"{call}: no [AIRGAP] marker (INSTRUMENT image?)")
        print(f"warm-up {self.a.warmup:.0f} s")
        self.wait(self.a.warmup)
        for call in self.nodes:
            if "gw" in self.roles[call]:
                st = info_state(self.nodes[call])
                print(f"{call}: run state {st}")
                if self.a.mock_host not in st.get("server", ""):
                    raise gb.SetupError(f"{call}: not on the mock server ({st.get('server')})")

    def restore(self) -> bool:
        ok = True
        for call in self.touched:
            n, st = self.nodes[call], self.state[call]
            n.quiesce_guard(30)
            try:
                if not n.link_ok():
                    n.emergency_reconnect(gb.LINK_LOST_RECONNECT_S, gb.LINK_LOST_RETRY_S)
                if "listener" in self.roles[call]:
                    n.cmd("--airgap off", 1.5, restore=True)
                if "gw" in self.roles[call]:
                    n.cmd("--gateway off", 3, restore=True)
                    n.cmd("--srvip 0.0.0.0", 2, restore=True)
                    if st["gateway"] == "on":
                        n.cmd("--gateway on", 3, restore=True)
                if "info" in st:
                    n.cmd(f"--setinfo {st['info']}", 1.5, restore=True)
                if "mesh" in st:
                    n.cmd(f"--mesh {st['mesh']}", 1.5, restore=True)
                if "gw" in self.roles[call]:
                    n.pause(8)
                now = info_state(n)
                bad = [k for k in ("gateway", "info", "mesh") if k in st and now.get(k) != st[k]]
                if "gw" in self.roles[call] and self.a.mock_host in now.get("server", ""):
                    bad.append("server still mock")
                if bad:
                    ok = False
                    print(f"!!! NODE LEFT IN BENCH STATE: {call}: {bad} now={now}", file=sys.stderr)
                else:
                    print(f"{call}: restored {now}")
            except Exception as e:
                ok = False
                print(f"!!! NODE LEFT IN BENCH STATE: {call}: restore failed {e!r}", file=sys.stderr)
        return ok

    def send(self, i: int) -> int:
        n = self.nodes[self.sender[0]]
        s = {c: len(x.lines) for c, x in self.nodes.items()}
        n.cmd(f"::{{{self.a.group}}}GWACK01 {self.a.tag} {i + 1}/{self.a.count}", 0.2)
        return s[self.sender[0]]

    def run_timing(self) -> list[dict]:
        marks = []
        for i in range(self.a.count):
            marks.append({c: len(x.lines) for c, x in self.nodes.items()})
            self.send(i)
            print(f"text {i + 1}/{self.a.count}")
            self.wait(self.a.interval)
        marks.append({c: len(x.lines) for c, x in self.nodes.items()})
        return [{"i": i, "from": marks[i], "to": marks[i + 1]} for i in range(self.a.count)]

    def run_missfirst(self) -> list[dict]:
        lst = self.nodes[self.listener[0]]
        gw = self.nodes[self.a.watch]
        out = []
        for i in range(self.a.count):
            s = len(lst.lines)
            lst.cmd("--airgap on", 1.0)
            if not lst.wait_for(r"\[AIRGAP\];on", s, 5):
                raise gb.RunAborted(f"{lst.call}: --airgap on not confirmed")
            mark = {c: len(x.lines) for c, x in self.nodes.items()}
            self.send(i)
            first = gw.wait_for(OWN_TX_RE.pattern, mark[gw.call], 40)
            t_first = time.monotonic()
            if first:
                self.wait(self.a.release_delay)
            s2 = len(lst.lines)
            lst.cmd("--airgap off", 0.2)
            released = lst.wait_for(r"\[AIRGAP\];off", s2, 5) is not None
            dt = time.monotonic() - t_first
            aid = OWN_TX_RE.search(first).group(1) if first else None
            print(f"trial {i + 1}: first copy {aid or 'NONE'}, released {released} after {dt:.2f} s")
            out.append({"i": i, "first_id": aid, "release_s": round(dt, 2), "released": released,
                        "from": mark})
            self.wait(self.a.interval)
        end = {c: len(x.lines) for c, x in self.nodes.items()}
        for k, t in enumerate(out):
            t["to"] = out[k + 1]["from"] if k + 1 < len(out) else end
        return out

    def analyse(self, windows: list[dict]) -> dict:
        res: dict = {"phase": self.a.phase, "info_mode": self.a.info_mode, "tag": self.a.tag,
                     "per_gw": {}, "heard": {}, "listeners": {}, "relay": {}, "trials": []}
        pairs = {c: pair_acks(own_ack_tx(self.nodes[c].lines))
                 for c in self.nodes if "gw" in self.roles[c]}
        for call, rows in pairs.items():
            kinds = [r["kind"] for r in rows]
            res["per_gw"][call] = {"rows": rows, "summary": {k: kinds.count(k) for k in set(kinds)}}
        own_ids = {x for rows in pairs.values() for r in rows for x in r["ids"]}
        for call, n in self.nodes.items():
            res["heard"][call] = heard_acks(n.lines, own_ids)
            res["listeners"][call] = listener_tally(pairs, res["heard"][call], call)
        res["relay"]["per_pair"] = relay_per_pair(pairs, res["heard"])
        if self.a.relay:
            n = self.nodes[self.a.relay]
            res["relay"][self.a.relay] = {
                m.group(1): 1 for m in map(OWN_TX_RE.search, n.lines)
                if m and m.group(1) in own_ids and m.group(2) not in ("84", "04")}
        if self.a.phase == "missfirst":
            lst = self.listener[0]
            for w in windows:
                seg = self.nodes[lst].lines[w["from"][lst]:w["to"][lst]]
                got = [(m.group(1), m.group(2), m.group(4)) for m in map(RX_ACK_RE.search, seg)
                       if m and m.group(1) == w["first_id"]]
                res["trials"].append({k: w[k] for k in ("i", "first_id", "release_s", "released")}
                                     | {"listener_rx": got})
        return res

    def main(self) -> int:
        rc = 0
        res = None
        try:
            self.setup()
            windows = self.run_timing() if self.a.phase == "timing" else self.run_missfirst()
            self.wait(self.a.tail)
            res = self.analyse(windows)
        except (gb.SetupError, gb.RunAborted) as e:
            print(f"run stopped: {e}")
            rc = 2
        except KeyboardInterrupt:
            print("interrupted")
            rc = 130
        finally:
            ok = self.restore()
            for n in self.nodes.values():
                n.close()
            if not ok:
                rc = rc or 3
        if res is not None:
            path = self.a.log_dir / f"gwack-{self.a.tag}-{self.stamp}.json"
            path.write_text(json.dumps(res, indent=1))
            for call, g in res["per_gw"].items():
                print(f"{call}: {g['summary']}")
            for call, t in res["listeners"].items():
                print(f"listener {call}: {t}")
            print(f"relay: {res['relay']}")
            for t in res["trials"]:
                print(f"trial {t['i'] + 1}: {t['first_id']} listener {t['listener_rx']}")
            print(f"summary: {path}")
        return rc


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--phase", choices=("timing", "missfirst"), default="timing")
    ap.add_argument("--gw", action="append", default=[], metavar="CALL=TRANSPORT")
    ap.add_argument("--sender", metavar="CALL=TRANSPORT")
    ap.add_argument("--listener", metavar="CALL=TRANSPORT", help="missfirst: INSTRUMENT node with --airgap")
    ap.add_argument("--watch", help="missfirst: gateway whose first ACK copy releases the listener")
    ap.add_argument("--relay", help="node (by call) set to --mesh on for the run")
    ap.add_argument("--mock-host")
    ap.add_argument("--info-mode", choices=("on", "off"), default="off")
    ap.add_argument("--group", default="9999")
    ap.add_argument("--count", type=int, default=8)
    ap.add_argument("--interval", type=float, default=40.0)
    ap.add_argument("--warmup", type=float, default=20.0)
    ap.add_argument("--tail", type=float, default=20.0)
    ap.add_argument("--release-delay", type=float, default=1.5,
                    help="missfirst: seconds after the first copy's TX line before --airgap off")
    ap.add_argument("--log-dir", type=Path, default=gb.DEFAULT_LOG_DIR)
    ap.add_argument("--tag", default="run")
    ap.add_argument("--reanalyse", metavar="STAMP",
                    help="offline: pair the ACKs in gwack-<tag>-<call>-STAMP.log (calls from --gw "
                         "and --sender, given as CALL=-) and write gwack-<tag>-STAMP-pairs.json")
    return ap


def reanalyse(a: argparse.Namespace) -> dict:
    """Offline pairing of saved node logs: per gateway pairs, listener tallies, relay per pair."""
    gws = [s.split("=")[0] for s in a.gw]
    calls = list(dict.fromkeys(gws + ([a.sender.split("=")[0]] if a.sender else [])))
    lines = {c: (a.log_dir / f"gwack-{a.tag}-{c.lower()}-{a.reanalyse}.log").read_text(
        errors="replace").splitlines() for c in calls}
    pairs = {c: pair_acks(own_ack_tx(lines[c])) for c in gws}
    own_ids = {x for rows in pairs.values() for r in rows for x in r["ids"]}
    heard = {c: heard_acks(lines[c], own_ids) for c in calls}
    res = {"tag": a.tag, "stamp": a.reanalyse,
           "per_gw": {c: {"summary": {k: [r["kind"] for r in rows].count(k)
                                      for k in {r["kind"] for r in rows}}, "rows": rows}
                      for c, rows in pairs.items()},
           "listeners": {c: listener_tally(pairs, heard[c], c) for c in calls},
           "relay_per_pair": relay_per_pair(pairs, heard)}
    out = a.log_dir / f"gwack-{a.tag}-{a.reanalyse}-pairs.json"
    out.write_text(json.dumps(res, indent=1))
    print(json.dumps({k: v for k, v in res.items() if k != "per_gw"}
                     | {"per_gw": {c: g["summary"] for c, g in res["per_gw"].items()}}, indent=1))
    print(f"-> {out}")
    return res


def main(argv: Optional[list[str]] = None) -> int:
    a = build_parser().parse_args(argv)
    if a.reanalyse:
        reanalyse(a)
        return 0
    if not (a.gw and a.sender and a.mock_host):
        raise SystemExit("a bench run needs --gw, --sender and --mock-host")
    if a.group not in ALLOWED_GROUPS:
        raise SystemExit(f"group {a.group!r} not allowed (bench rule: {ALLOWED_GROUPS})")
    for spec in a.gw + [a.sender] + ([a.listener] if a.listener else []):
        call = gb.parse_node(spec)[0]
        if not gb.OWN_CALL_RE.fullmatch(call):
            raise SystemExit(f"{call}: not an own DK5EN-NN callsign")
    if a.phase == "missfirst" and not (a.listener and a.watch):
        raise SystemExit("missfirst needs --listener and --watch")
    return Bench(a).main()


if __name__ == "__main__":
    sys.exit(main())
