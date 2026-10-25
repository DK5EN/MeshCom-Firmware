"""Own-message echo bench driver: is an own text released before its first LoRa TX?

Background (src/lora_functions.cpp echo release, src/pn_retry.h pnFrameIsReleasableGroupText):
an echo heard while the own TX slot is still READY sets the slot DONE, so a group text can leave
the node without ever going on air. This driver sends marked texts to group 9999 / TEST or as
a PN between the own bench nodes and reads, from the console logs of every node, per message:
  sender   RELEASED (echo on READY slot, no TX) | SENT (TX-LoRa / RING_TX_READ) | UNKNOWN
  PN       ACK_SKIP_READY, own TX count incl. retries, ACK type (server / binary / already)
  others   queued from server (RING_WRITE src=udp_rx), transmitted, heard (+path)
  field    [LOG] lines for the msg id whose path contains a field callsign (DL2JA-1, ...)

Msg-id correlation: the sender logs `NEW-TXT`/`TX-UDP`/`TX-LoRa` lines of the form
`x<ID> H04 S0 T0 M00 <SRC>><DST>:<text>`; the line carrying the unique marker text gives the id.
`[RETX] echo on READY slot, released retid:N` has no id: the next `checkOwnTx:<ID>` line does.

Cases are `NAME:SENDER:DEST` (DEST = 9999 | TEST | @DK5EN-n for a PN) and are sent round-robin:
every --interval seconds each case sends once, case k offset by k * interval / len(cases).

Hard guard (applied before anything is opened): group only 9999 / TEST; PN only to
DK5EN-1/-14/-90/-92/-98; sender one of those and a --node; only a documented console command
set is ever sent (no --gateway, --setgrc, --mesh, --txpower, --reboot).

Example (run A):
  python3 tools/bench/own_echo_bench.py --tag A \\
      --node DK5EN-98=tcp:192.168.68.65 --node DK5EN-1=tcp:192.168.68.73 \\
      --case E1:DK5EN-98:9999 --case E6:DK5EN-1:9999

Passwords come from .env (MC_PW_<CALL>) via mc_console, never from the command line.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import threading
import time
from collections import Counter
from pathlib import Path
from typing import NamedTuple, Optional

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools"))

import gwrx_bench as gb  # noqa: E402

ALLOWED_GROUPS = ("9999", "TEST")
BENCH_CALLS = ("DK5EN-1", "DK5EN-14", "DK5EN-90", "DK5EN-92", "DK5EN-98")
FIELD_CALLS = ("DL2JA-1", "DL2JA-2", "DB0ED-99", "DL2UD-1", "DF2SI-12")
CASE_NAME_RE = re.compile(r"[A-Za-z0-9]{1,8}")
STATIC_CMDS = frozenset(
    ["--info", "--loradebug on", "--loradebug off", "--txcapture on", "--txcapture off",
     "--airgap on", "--airgap off"]
)
MSG_CMD_RE = re.compile(
    r"::\{(9999|TEST|" + "|".join(re.escape(c) for c in BENCH_CALLS)
    + r")\}EC [A-Za-z0-9]{1,8} \d+/\d+ \d{6}"
)

# ---- log line shapes (rpizero logger and gb.Node log) ----
TS_FULL_RE = re.compile(r"^\d{4}-\d{2}-\d{2} (\d\d):(\d\d):(\d\d)\.(\d{3})  (.*)$")
TS_NODE_RE = re.compile(r"^(\d\d):(\d\d):(\d\d) (.*)$")
RW_RE = re.compile(r"RING_WRITE slot=\d+ type=\w+ status=\w+ len=\d+ msg_id=([0-9A-F]{8}) queued=\S+ src=(\w+)")
TXREAD_RE = re.compile(r"RING_TX_READ slot=\d+ prio=\d+ type=\w+ status=\w+ len=\d+ msg_id=([0-9A-F]{8}) retry=(\d+)")
TXLORA_RE = re.compile(r"TX-LoRa\s+\d+ : x([0-9A-F]{8}) ")
TXUDP_RE = re.compile(r"TX-UDP\s+\d+ : x([0-9A-F]{8}) ")
OWNLINE_RE = re.compile(r"(?:NEW-TXT|TX-UDP|TX-LoRa)\s+\d+ : x([0-9A-F]{8}) ")
ECHO_RE = re.compile(r"\[RETX\] echo on READY slot, released retid:(\d+)")
CHKOWN_RE = re.compile(r"checkOwnTx:([0-9A-F]{8}) own_msg_id")
ACKSKIP_RE = re.compile(r"ACK_SKIP_READY slot=\d+ msg_id=([0-9A-F]{8})")
RETRANS_RE = re.compile(r"RETRANSMIT retry=(\d+) \S+ msg_id=([0-9A-F]{8})")
ACK_RE = re.compile(
    r"\[RETX\] (?:(server|binary) ACK for retid:\d+ stop retransmit|PN already acked, stop retid:\d+)"
    r" ?(?:msg-id:+-?)?([0-9A-F]{8})?"
)
LOG_RE = re.compile(r"\[LOG\] \S+ : x([0-9A-F]{8}) (?:\S+ ){4}([^\s>]+)>([^\s:]+):(.*)")
SEND_RE = re.compile(r"^\S+ >>> ::\{([^}]+)\}(EC (\w+) (\d+)/(\d+) (\d{6}))\s*$")
INFO_RE = {
    "loradebug": re.compile(r"\.\.\.LORADEBUG (on|off)"),
    "txcapture": re.compile(r"\.\.\.TXCAPTURE (on|off)"),
}
CHK_LOOKAHEAD = 8  # lines between the release line and its checkOwnTx line


# ------------------------------------------------------------------- guard


class Case(NamedTuple):
    name: str
    sender: str
    dest: str  # "9999" | "TEST" | PN callsign
    pn: bool


def check_dest(dest: str) -> tuple[bool, str]:
    """(is_pn, target). Group only 9999/TEST; PN only `@<bench call>`. Everything else refused."""
    if dest in ALLOWED_GROUPS:
        return False, dest
    if dest.startswith("@") and dest[1:] in BENCH_CALLS:
        return True, dest[1:]
    raise gb.PlanError(
        f"destination {dest!r} refused: group {ALLOWED_GROUPS} or PN @{{{','.join(BENCH_CALLS)}}} only"
    )


def parse_case(spec: str) -> Case:
    parts = spec.split(":")
    if len(parts) != 3 or not all(parts):
        raise gb.PlanError(f"bad --case {spec!r}: expected NAME:SENDER:DEST")
    name, sender, dest = parts
    if not CASE_NAME_RE.fullmatch(name):
        raise gb.PlanError(f"bad case name {name!r}")
    sender = sender.upper()
    if sender not in BENCH_CALLS:
        raise gb.PlanError(f"sender {sender!r} refused: not one of {BENCH_CALLS}")
    pn, target = check_dest(dest)
    if pn and target == sender:
        raise gb.PlanError(f"case {name}: PN to the sender itself refused")
    return Case(name, sender, target, pn)


def check_cases(cases: list[Case], nodes: list[str], airgap: list[str]) -> None:
    if not cases:
        raise gb.PlanError("no --case given")
    names = [c.name for c in cases]
    if len(set(names)) != len(names):
        raise gb.PlanError("duplicate case names")
    for c in cases:
        if c.sender not in nodes:
            raise gb.PlanError(f"case {c.name}: sender {c.sender} is not a --node")
        if c.sender in airgap:
            raise gb.PlanError(f"case {c.name}: sender {c.sender} is --airgap on")
    for a in airgap:
        if a not in nodes:
            raise gb.PlanError(f"--airgap {a} is not a --node")


def check_node_call(call: str) -> None:
    if call not in BENCH_CALLS:
        raise gb.PlanError(f"node {call!r} refused: not one of {BENCH_CALLS}")


def check_console_cmd(cmd: str) -> None:
    """Only the documented command set may reach a console."""
    if cmd in STATIC_CMDS or MSG_CMD_RE.fullmatch(cmd):
        return
    raise gb.PlanError(f"console command {cmd!r} is not in the documented set")


def build_text(case: str, i: int, n: int, stamp: str) -> str:
    """Unique ASCII marker: `EC <case> <i>/<n> <HHMMSS>`."""
    return f"EC {case} {i}/{n} {stamp}"


def console_line(case: Case, text: str) -> str:
    return f"::{{{case.dest}}}{text}"


def schedule(cases: list[Case], count: int, interval: float) -> list[tuple[float, int, int]]:
    """(t offset s, case index, message number 1..count), time ordered; case k offset k*interval/len."""
    rows = [(k * interval / len(cases) + (i - 1) * interval, k, i)
            for i in range(1, count + 1) for k in range(len(cases))]
    return sorted(rows)


# ---------------------------------------------------------------- analysis


def norm_line(raw: str) -> tuple[Optional[float], str]:
    """(seconds of day or None, body) for the rpizero logger and the gb.Node log format."""
    m = TS_FULL_RE.match(raw)
    if m:
        return int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]) + int(m[4]) / 1000, m[5]
    m = TS_NODE_RE.match(raw)
    if m:
        return int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]), m[4]
    return None, raw


def dt(a: Optional[float], b: Optional[float]) -> Optional[float]:
    if a is None or b is None:
        return None
    d = b - a
    return round(d + 86400 if d < -43200 else d, 2)


class NodeEvents:
    """Everything one node's log says, keyed by msg id. Entries are (line index, time, ...)."""

    def __init__(self, lines: list[str]) -> None:
        self.txlora: dict[str, list] = {}
        self.txread: dict[str, list] = {}
        self.txudp: dict[str, list] = {}
        self.rw: dict[str, list] = {}
        self.released: dict[str, list] = {}
        self.ackskip: dict[str, list] = {}
        self.retrans: dict[str, list] = {}
        self.acks: dict[str, list] = {}
        self.heard: dict[str, list] = {}
        self.own: list[tuple[int, Optional[float], str, str]] = []  # idx, t, id, body
        self.rows: list[tuple[int, Optional[float], str]] = []
        pending: Optional[tuple[int, Optional[float]]] = None
        for idx, raw in enumerate(lines):
            t, body = norm_line(raw.rstrip("\n"))
            self.rows.append((idx, t, body))
            if pending is not None:
                c = CHKOWN_RE.search(body)
                if c:
                    self.released.setdefault(c[1], []).append((pending[0], pending[1]))
                    pending = None
                elif idx - pending[0] > CHK_LOOKAHEAD:
                    pending = None
            if ECHO_RE.search(body):
                pending = (idx, t)
                continue
            if (m := RW_RE.search(body)):
                self.rw.setdefault(m[1], []).append((idx, t, m[2]))
            elif (m := TXLORA_RE.search(body)):
                self.txlora.setdefault(m[1], []).append((idx, t))
            elif (m := TXREAD_RE.search(body)):
                self.txread.setdefault(m[1], []).append((idx, t, int(m[2])))
            elif (m := ACKSKIP_RE.search(body)):
                self.ackskip.setdefault(m[1], []).append((idx, t))
            elif (m := RETRANS_RE.search(body)):
                self.retrans.setdefault(m[2], []).append((idx, t, int(m[1])))
            elif (m := ACK_RE.search(body)):
                kind = m[1] or "already"
                if m[2]:
                    self.acks.setdefault(m[2], []).append((idx, t, kind))
            elif (m := LOG_RE.search(body)):
                self.heard.setdefault(m[1], []).append((idx, t, m[2].split(","), m[3], m[4]))
            if (m := OWNLINE_RE.search(body)):
                self.own.append((idx, t, m[1], body))
            if TXUDP_RE.search(body):
                m = TXUDP_RE.search(body)
                self.txudp.setdefault(m[1], []).append((idx, t))

    def find_id(self, marker: str) -> Optional[tuple[str, int, Optional[float]]]:
        """First NEW-TXT/TX-UDP/TX-LoRa line carrying the marker text -> (id, idx, t)."""
        pat = re.compile(re.escape(marker) + r"(?!\d)")
        for idx, t, mid, body in self.own:
            if pat.search(body):
                return mid, idx, t
        return None

    def tx_times(self, mid: str) -> list[tuple[int, Optional[float]]]:
        lora = [(i, t) for i, t, *_ in self.txlora.get(mid, [])]
        read = [(i, t) for i, t, *_ in self.txread.get(mid, [])]
        return lora if len(lora) >= len(read) else read


def heard_summary(ev: NodeEvents, mid: str) -> list[dict]:
    return [{"idx": i, "t": t, "path": path, "dst": dst} for i, t, path, dst, _ in ev.heard.get(mid, [])]


def field_calls(ev: NodeEvents, mid: str, field: tuple[str, ...]) -> list[str]:
    seen = []
    for _, _, path, _, _ in ev.heard.get(mid, []):
        for c in path:
            if c in field and c not in seen:
                seen.append(c)
    return seen


def analyse_message(send: dict, events: dict[str, NodeEvents], field: tuple[str, ...]) -> dict:
    sender = send["sender"]
    sev = events[sender]
    out: dict = {k: send[k] for k in ("case", "i", "marker", "sender", "dest")}
    found = sev.find_id(send["marker"])
    if not found:
        out.update(id=None, verdict="UNKNOWN", reason="marker not seen on the sender")
        return out
    mid, w_idx, w_t = found
    out["id"] = mid
    tx = sev.tx_times(mid)
    rel = sev.released.get(mid, [])
    heard = sev.heard.get(mid, [])
    first_tx = min(tx)[0] if tx else None
    if tx:
        verdict = "SENT"
    elif rel:
        verdict = "RELEASED"
    else:
        verdict = "UNKNOWN"
    out["verdict"] = verdict
    out["own_tx"] = len(tx)
    out["first_tx_after_write_s"] = dt(w_t, min(tx)[1]) if tx else None
    if rel:
        out["release_after_write_s"] = dt(w_t, rel[0][1])
        after = [h for h in heard if h[0] >= rel[0][0]] or heard
        out["released_by"] = after[0][2][-1] if after else None
        out["released_and_sent"] = bool(tx)
    if tx and heard:
        out["echo_vs_own_tx"] = "before" if heard[0][0] < first_tx else "after"
    elif tx:
        out["echo_vs_own_tx"] = "none"
    if send["pn"]:
        retrans = sorted(r for *_, r in sev.retrans.get(mid, []))
        out["pn"] = {
            "ack_skip_ready": bool(sev.ackskip.get(mid)),
            "own_tx": len(tx),
            "retransmit_retries": retrans,
            "acks": [k for *_, k in sev.acks.get(mid, [])],
        }
    nodes = {}
    for call, ev in events.items():
        ntx = ev.tx_times(mid)
        nodes[call] = {
            "queued_from_server": any(src == "udp_rx" for *_, src in ev.rw.get(mid, [])),
            "tx": len(ntx),
            "heard": heard_summary(ev, mid),
            "field": field_calls(ev, mid, field),
        }
    out["nodes"] = nodes
    out["field_echo"] = sorted({c for n in nodes.values() for c in n["field"]})
    return out


def analyse(sends: list[dict], lines: dict[str, list[str]], field: tuple[str, ...] = FIELD_CALLS) -> dict:
    events = {c: NodeEvents(ls) for c, ls in lines.items()}
    msgs = [analyse_message(s, events, field) for s in sends]
    cases: dict = {}
    for m in msgs:
        c = cases.setdefault(m["case"], {
            "sender": m["sender"], "dest": m["dest"], "sent": 0, "released": 0, "tx": 0,
            "unknown": 0, "released_by": Counter(), "field_echo": Counter(),
            "pn_ack_skip": 0, "pn_tx_total": 0, "pn_acks": Counter()})
        c["sent"] += 1
        c[{"RELEASED": "released", "SENT": "tx", "UNKNOWN": "unknown"}[m["verdict"]]] += 1
        if m["verdict"] == "RELEASED" and m.get("released_by"):
            c["released_by"][m["released_by"]] += 1
        c["field_echo"].update(m.get("field_echo", []))
        if "pn" in m:
            c["pn_ack_skip"] += m["pn"]["ack_skip_ready"]
            c["pn_tx_total"] += m["pn"]["own_tx"]
            c["pn_acks"].update(m["pn"]["acks"])
    for c in cases.values():
        for k in ("released_by", "field_echo", "pn_acks"):
            c[k] = dict(c[k])
    return {"cases": cases, "messages": msgs}


def extract_sends(call: str, lines: list[str]) -> list[dict]:
    """Recover the sends from the `>>> ::{DEST}EC ...` command echoes of a saved node log."""
    out = []
    for ln in lines:
        m = SEND_RE.match(ln)
        if m:
            out.append({"case": m[3], "i": int(m[4]), "n": int(m[5]), "marker": m[2], "sender": call,
                        "dest": m[1], "pn": m[1] not in ALLOWED_GROUPS})
    return out


def format_cases(res: dict) -> list[str]:
    rows = []
    for name, c in sorted(res["cases"].items()):
        rows.append(
            f"{name} {c['sender']}->{c['dest']}: sent={c['sent']} released={c['released']} "
            f"tx={c['tx']} unknown={c['unknown']} released_by={c['released_by']} "
            f"field_echo={c['field_echo']}"
            + (f" pn_ack_skip={c['pn_ack_skip']} pn_tx={c['pn_tx_total']} acks={c['pn_acks']}"
               if c["pn_tx_total"] or c["pn_ack_skip"] or c["pn_acks"] else ""))
    return rows


# ------------------------------------------------------------------ driver


def info_flags(lines: list[str]) -> dict:
    st: dict = {}
    for ln in lines:
        for key, rx in INFO_RE.items():
            m = rx.search(ln)
            if m and key not in st:
                st[key] = m.group(1)
    return st


class Bench:
    def __init__(self, a: argparse.Namespace, nodes: list[tuple[str, str, str]], cases: list[Case]) -> None:
        self.a = a
        self.specs = nodes
        self.cases = cases
        self.airgap = [c.upper() for c in a.airgap]
        self.stamp = time.strftime("%Y%m%d-%H%M%S")
        self.mark = self.stamp[-6:]
        self.nodes: dict[str, gb.Node] = {}
        self.state: dict[str, dict] = {}
        self.touched: list[str] = []
        self.sends: list[dict] = []
        self.abort = threading.Event()
        self.reasons: list[str] = []

    def log_path(self, call: str) -> Path:
        return self.a.log_dir / f"own-echo-{self.a.tag}-{call.lower()}-{self.stamp}.log"

    def on_reboot(self, n: gb.Node, line: str) -> None:
        msg = f"{n.call}: REBOOT detected ({line.strip()!r})"
        self.reasons.append(msg)
        print(msg)
        self.abort.set()

    def wait(self, secs: float) -> None:
        if self.abort.wait(max(0.0, secs)):
            raise gb.RunAborted("; ".join(self.reasons))

    def cmd(self, n: gb.Node, c: str, wait: float, restore: bool = False) -> None:
        check_console_cmd(c)
        n.cmd(c, wait, restore=restore)

    def setup(self) -> None:
        self.a.log_dir.mkdir(parents=True, exist_ok=True)
        for call, kind, value in self.specs:
            self.nodes[call] = gb.Node(call, gb.make_transport(kind, value, call), self.log_path(call))
        for n in self.nodes.values():
            n.abort_event, n.on_reboot = self.abort, self.on_reboot
            n.start()
            s = len(n.lines)
            self.cmd(n, "--info", 3)
            self.state[n.call] = info_flags(n.lines[s:])
            print(f"{n.call}: start state {self.state[n.call]}")
            if "loradebug" not in self.state[n.call]:
                print(f"WARNING {n.call}: --info has no LORADEBUG; --loradebug stays on after the run")
        for n in self.nodes.values():
            n.watch_boot = True
        for call, n in self.nodes.items():
            self.touched.append(call)
            self.cmd(n, "--loradebug on", 1.0)
            self.cmd(n, "--txcapture on", 1.0)
            if call in self.airgap:
                s = len(n.lines)
                self.cmd(n, "--airgap on", 1.0)
                if not n.wait_for(r"\[AIRGAP\];on", s, 5):
                    raise gb.SetupError(f"{call}: no [AIRGAP];on within 5 s (INSTRUMENT image?)")
        print(f"warm-up {self.a.warmup:.0f} s")
        self.wait(self.a.warmup)

    def restore(self) -> bool:
        ok = True
        for call in self.touched:
            n, st = self.nodes[call], self.state[call]
            n.quiesce_guard(30)
            try:
                if not n.link_ok():
                    n.emergency_reconnect(gb.LINK_LOST_RECONNECT_S, gb.LINK_LOST_RETRY_S)
                if call in self.airgap:
                    self.cmd(n, "--airgap off", 1.5, restore=True)
                if "loradebug" in st:
                    self.cmd(n, f"--loradebug {st['loradebug']}", 1.0, restore=True)
                self.cmd(n, f"--txcapture {st.get('txcapture', 'off')}", 1.0, restore=True)
                s = len(n.lines)
                self.cmd(n, "--info", 3, restore=True)
                now = info_flags(n.lines[s:])
                want = {"txcapture": st.get("txcapture", "off")}
                if "loradebug" in st:
                    want["loradebug"] = st["loradebug"]
                bad = [k for k, v in want.items() if now.get(k) != v]
                if bad:
                    ok = False
                    print(f"!!! NODE LEFT IN BENCH STATE: {call}: {bad} now={now}", file=sys.stderr)
                else:
                    print(f"{call}: restored {now}")
            except Exception as e:
                ok = False
                print(f"!!! NODE LEFT IN BENCH STATE: {call}: restore failed {e!r}", file=sys.stderr)
        return ok

    def run_sends(self) -> None:
        t0 = time.monotonic()
        for off, k, i in schedule(self.cases, self.a.count, self.a.interval):
            self.wait(t0 + off - time.monotonic())
            case = self.cases[k]
            text = build_text(case.name, i, self.a.count, self.mark)
            self.cmd(self.nodes[case.sender], console_line(case, text), 0.2)
            self.sends.append({"case": case.name, "i": i, "n": self.a.count, "marker": text,
                               "sender": case.sender, "dest": case.dest, "pn": case.pn})
            print(f"{case.name} {i}/{self.a.count} {case.sender} -> {case.dest}")

    def main(self) -> int:
        rc = 0
        try:
            self.setup()
            self.run_sends()
            self.wait(self.a.tail)
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
        if self.sends:
            lines = {c: self.log_path(c).read_text(errors="replace").splitlines() for c in self.nodes}
            res = analyse(self.sends, lines, tuple(self.a.field))
            res |= {"tag": self.a.tag, "stamp": self.stamp, "rc": rc, "reasons": self.reasons,
                    "sends": self.sends}
            path = self.a.log_dir / f"own-echo-{self.a.tag}-{self.stamp}.json"
            path.write_text(json.dumps(res, indent=1))
            print("\n".join(format_cases(res)))
            print(f"summary: {path}")
        return rc


def reanalyse_group(files: dict[str, Path], field: tuple[str, ...]) -> dict:
    lines = {c: p.read_text(errors="replace").splitlines() for c, p in files.items()}
    sends = [s for c, ls in lines.items() for s in extract_sends(c, ls)]
    return analyse(sends, lines, field) | {"sends": sends}


LOGNAME_RE = re.compile(r"own-echo-(.+)-(dk5en-\d+)-(\d{8}-\d{6})\.log")


def find_groups(directory: Path, stamp: Optional[str] = None, tag: Optional[str] = None
                ) -> dict[tuple[str, str], dict[str, Path]]:
    groups: dict[tuple[str, str], dict[str, Path]] = {}
    for p in sorted(directory.glob("own-echo-*.log")):
        m = LOGNAME_RE.fullmatch(p.name)
        if m and (stamp is None or m[3] == stamp) and (tag is None or m[1] == tag):
            groups.setdefault((m[1], m[3]), {})[m[2].upper()] = p
    return groups


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--node", action="append", default=[], metavar="CALL=TRANSPORT")
    ap.add_argument("--case", action="append", default=[], metavar="NAME:SENDER:DEST")
    ap.add_argument("--airgap", action="append", default=[], metavar="CALL")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--interval", type=float, default=90.0)
    ap.add_argument("--warmup", type=float, default=20.0)
    ap.add_argument("--tail", type=float, default=60.0, help="seconds after the last send (PN: 180)")
    ap.add_argument("--field", action="append", default=None, metavar="CALL",
                    help=f"field callsigns for the echo check (default {', '.join(FIELD_CALLS)})")
    ap.add_argument("--log-dir", type=Path, default=gb.DEFAULT_LOG_DIR)
    ap.add_argument("--tag", default="run")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, open nothing")
    ap.add_argument("--reanalyse", metavar="STAMP", help="re-run the analysis on saved logs of STAMP in --log-dir")
    ap.add_argument("--reanalyse-dir", type=Path, metavar="DIR", help="re-run the analysis on all saved logs in DIR")
    return ap


def plan_text(a: argparse.Namespace, specs: list, cases: list[Case]) -> str:
    out = ["nodes: " + ", ".join(f"{c}={k}:{v}" for c, k, v in specs),
           "airgap: " + (", ".join(a.airgap) or "-"),
           f"count={a.count} interval={a.interval}s warmup={a.warmup}s tail={a.tail}s"]
    for c in cases:
        out.append(f"case {c.name}: {c.sender} -> {'PN ' if c.pn else 'group '}{c.dest}: guard OK")
    for off, k, i in schedule(cases, a.count, a.interval):
        c = cases[k]
        out.append(f"  t+{off:7.1f}s {c.name} {i}/{a.count} {c.sender}: "
                   f"{console_line(c, build_text(c.name, i, a.count, 'HHMMSS'))}")
    last = max(o for o, *_ in schedule(cases, a.count, a.interval))
    out.append(f"total on air: about {(a.warmup + last + a.tail) / 60:.1f} min")
    return "\n".join(out)


def main(argv: Optional[list[str]] = None) -> int:
    a = build_parser().parse_args(argv)
    field = tuple(a.field) if a.field else FIELD_CALLS
    a.field = list(field)
    if a.reanalyse or a.reanalyse_dir:
        directory = a.reanalyse_dir or a.log_dir
        groups = find_groups(directory, a.reanalyse, None if a.reanalyse_dir else a.tag if a.tag != "run" else None)
        if not groups:
            raise SystemExit(f"no own-echo logs found in {directory}")
        for (tag, stamp), files in groups.items():
            res = reanalyse_group(files, field)
            out = directory / f"own-echo-{tag}-{stamp}-reanalysis.json"
            out.write_text(json.dumps(res, indent=1))
            print(f"{tag} {stamp}: nodes {sorted(files)}")
            print("\n".join(format_cases(res)))
            print(f"-> {out}")
        return 0
    try:
        specs = [gb.parse_node(s) for s in a.node]
        for call, *_ in specs:
            check_node_call(call)
        cases = [parse_case(s) for s in a.case]
        airgap = [c.upper() for c in a.airgap]
        check_cases(cases, [c for c, *_ in specs], airgap)
        if a.count < 1 or a.interval <= 0:
            raise gb.PlanError("--count >= 1 and --interval > 0 needed")
    except gb.PlanError as e:
        raise SystemExit(f"refused: {e}") from e
    if a.dry_run:
        print(plan_text(a, specs, cases))
        return 0
    return Bench(a, specs, cases).main()


if __name__ == "__main__":
    sys.exit(main())
