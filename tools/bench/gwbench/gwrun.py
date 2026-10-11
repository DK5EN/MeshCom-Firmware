#!/usr/bin/env python3
"""RM + plain-traffic regression DK5EN-1 (Heltec) <-> DK5EN-90 (RAK), gateway on, against a mock server on the rpizero.

Addresses are DHCP and move: the Heltec is resolved by dk5en-1.local, the RAK (no mDNS) comes from --rak-host; both web pages
must name the expected callsign before anything is sent (identity check on `GET /`).

SAFE ORDER (each step confirmed from the log before the next):
  guard    identity per web page and identity_guard on both; abort if `--info` shows gateway already ON.
  mock     copy tools/mock/meshcom_server.py to rpizero:~/gwmock and start it there (UDP 1990) over ssh.
  srvip    `--srvip <rpizero ip>` (RAM only, no reboot); wait `[SRVIP];<ip>;set` (else not an instrument image -> ABORT, no gateway),
           then the applied line (RAK `[SRVIP];..;applied`, Heltec `[WIFI]..dns..ip..`).
  gw on    `--gateway on` (saved, no reboot) only now; the mock must log KEEP of both calls from both node IPs within 90 s.
  teardown ALWAYS: `--gateway off` first, THEN `--srvip 0.0.0.0` (clearing srvip while gateway is on would restart UDP
           towards the REAL server), RM at 90 restored, stop loggers + mock.
LoRa-only mode (--lora-only): no setup phase, no mock, no --srvip; the guard still insists on gateway OFF on both nodes.
Phase `repeat` (opt-in, INSTRUMENT image): RM1 repeat ladder tests with `--airgap on|off` (RAM only; drops all RX and own TX);
  teardown ALWAYS sends `--airgap off` to both nodes and waits for `[AIRGAP];off`.
Residual risk: gateway-on is persisted, srvip is RAM only: a reboot while gateway is on reconnects to the real server;
gwguard.BootGuard watches both logs and sends `--gateway off` on a boot marker.
Rules in code: destinations only DK5EN-1/DK5EN-90/TEST, only the two resolved node hosts, no radio/pos write, same-value write-back.
"""
import argparse, json, os, re, signal, socket, subprocess, sys, threading, time, urllib.error, urllib.parse, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gwguard
from typing import Any, Dict, List, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PWFILE = os.environ.get("GWBENCH_PWFILE", os.path.expanduser("~/MeshCom-bench-backups/bench-node-passwords-20261006.txt"))
MOCK_SSH = "rpizero.local"
MOCK_DIR = "gwmock"          # ~/gwmock on the pi
ALLOWED_HOSTS: set = set()   # filled from the resolved node addresses
ALLOWED_CALLS = {"DK5EN-1", "DK5EN-90", "TEST"}
RM_READ = ["sync", "status", "radio", "name", "atxt", "pos", "sens", "mh 0", "mh {PEER}", "txq", "mbox", "maxhop"]
PHASES = ["guard", "setup", "rm", "neg", "plain", "refused", "repeat", "teardown"]
DEFAULT_PHASES = "guard,setup,rm,neg,plain"
LORA_ONLY_PHASES = "guard,rm,neg,plain"
RM_WAIT = 200          # s: "No answer" comes after the whole repeat ladder (~170 s)
LADDER_END = 140       # s after /rmsend: copy 4 would be out by ~126 s; observe past it
GAP_OFF = 34           # s after copy 1 left the sender: airgap off (copy 2 is due ~42 s after copy 1). Neighbour relays
                       # re-radiate copy 1 and the reply for up to ~24 s (bench 2026-10-11: DK5EN-92/-14/-98); an earlier
                       # end lets a relay deliver them and copy 2 is never needed
REPLY_GAP_OFF = 40     # s after copy 1, lost reply: relays of relays carried the reply to +35 s after copy 1; a copy 2 that
                       # falls into the gap is dropped and copy 3 (~84 s) meets the cache instead
COPY1_WAIT = 20        # s after /rmsend ok when no TX-LoRa marker shows up (TX backlog delayed copy 1 by 9 s)
MOCK_IP = ""


class Abort(Exception):
    pass


class Node:
    def __init__(self, call: str, host: str, fleet: str) -> None:
        self.call, self.host, self.fleet, self.base = call, host, fleet, "http://" + host
        self.log = self.cmd = self.pw = ""
        self.proc: Optional[subprocess.Popen] = None

    def con(self, c: str) -> None:
        open(self.cmd, "a").write(c + "\n")

    def text(self, since: int = 0) -> str:
        try:
            with open(self.log, errors="replace") as f:
                f.seek(since); return f.read()
        except OSError:
            return ""

    def pos(self) -> int:
        return os.path.getsize(self.log) if os.path.exists(self.log) else 0

    def wait(self, rx: str, since: int, timeout: float) -> bool:
        t0 = time.time()
        while time.time() - t0 < timeout:
            if re.search(rx, self.text(since)):
                return True
            time.sleep(0.5)
        return False


def guard_call(c: str) -> str:
    if c not in ALLOWED_CALLS:
        raise Abort("bench rule: destination %r not allowed" % c)
    return c


def http(base: str, path: str, body: Optional[str] = None, timeout: int = 10) -> Any:
    if base.split("//")[1] not in ALLOWED_HOSTS:
        raise Abort("bench rule: host not allowed")
    hd = {"X-MC": "1"}
    if body is not None:
        hd["Content-Type"] = "application/x-www-form-urlencoded"
    try:
        with urllib.request.urlopen(urllib.request.Request(base + path, data=body.encode() if body is not None else None, headers=hd), timeout=timeout) as r:
            t = r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        t = e.read().decode("utf-8", "replace")
    try:
        return json.loads(t)
    except ValueError:
        return t


def q(s: str) -> str:
    return urllib.parse.quote(s, safe="")


def names_call(base: str, call: str) -> bool:
    """The node's start page names its own callsign (DK5EN-1 must not match DK5EN-12)."""
    page = http(base, "/")
    return isinstance(page, str) and re.search(re.escape(call) + r"(?![0-9])", page) is not None


def ssh(cmd: str, timeout: int = 60) -> subprocess.CompletedProcess:
    return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", MOCK_SSH, cmd], capture_output=True, text=True, timeout=timeout)


class Run:
    def __init__(self, a: argparse.Namespace) -> None:
        self.a = a
        self.dir = a.run_dir or os.path.join(REPO, "tools", "bench", "runs", "gwbench_" + time.strftime("%Y%m%d-%H%M%S"))
        self.n1 = Node("DK5EN-1", a.heltec_host or socket.gethostbyname("dk5en-1.local"), "heltec-1")
        self.n90 = Node("DK5EN-90", a.rak_host, "rak-90")
        self.nodes = [self.n1, self.n90]
        ALLOWED_HOSTS.update(n.host for n in self.nodes)
        self.mock: Optional[subprocess.Popen] = None
        self.events: List[Dict[str, Any]] = []
        self.srv_set = False
        self.guard: Optional[gwguard.BootGuard] = None
        self.raise_trip = True
        self.rm90_before: Optional[int] = None
        self.udplog_on = False
        self.dbg_on = False
        self.airgap_used = False

    def nap(self, sec: float) -> None:
        """sleep in slices; a boot-guard trip aborts the run into teardown."""
        end = time.time() + sec
        while True:
            if self.raise_trip and self.guard is not None and self.guard.tripped:
                raise Abort("BOOT GUARD tripped on %s: gateway off sent, aborting" % ",".join(self.guard.tripped))
            if time.time() >= end:
                return
            time.sleep(min(0.5, max(0.0, end - time.time())))

    def ev(self, case: str, d: str, **kw: Any) -> None:
        self.events.append({"case": case, "dir": d, "t": time.time(), **kw})
        json.dump(self.events, open(os.path.join(self.dir, "events.json"), "w"), indent=1)
        print("%-24s %-8s %s" % (case, d, kw), flush=True)

    def start_loggers(self) -> None:
        pws = {m.group(1): m.group(2) for m in (re.match(r"^(DK5EN-\d+)\s{2,}.+?\s{2,}(\S+)\s{2,}", l) for l in open(PWFILE)) if m}
        for n in self.nodes:
            n.log, n.cmd, n.pw = os.path.join(self.dir, n.call + ".log"), os.path.join(self.dir, n.call + ".cmd"), pws[n.call]
            open(n.cmd, "a").close()
        pf = os.path.join(self.dir, "pw1"); open(pf, "w").write(self.n1.pw); os.chmod(pf, 0o600)
        self.n1.proc = subprocess.Popen([sys.executable, os.path.join(HERE, "conlog.py"), self.n1.host, pf, self.n1.log, self.n1.cmd])
        self.n90.proc = subprocess.Popen([sys.executable, os.path.join(HERE, "gwseriallog.py"), self.a.rak_port, self.n90.log, self.n90.cmd])
        time.sleep(4)

    def mock_text(self) -> str:
        return open(os.path.join(self.dir, "mock.log"), errors="replace").read()

    def p_guard(self) -> None:
        for n in self.nodes:
            if not names_call(n.base, n.call):
                raise Abort("%s: the web page at %s does not name this callsign (addresses moved?)" % (n.call, n.host))
            self.ev("web_identity", n.call, host=n.host, result="ok")
        for n in self.nodes:
            p = n.pos(); n.con("--info"); time.sleep(6)
            info = n.text(p); f = os.path.join(self.dir, "info_%s.txt" % n.call); open(f, "w").write(info)
            r = subprocess.run([sys.executable, os.path.join(REPO, "tools/bench/identity_guard.py"), "--node", n.fleet, "--info-file", f], cwd=REPO, capture_output=True, text=True)
            self.ev("identity_guard", n.call, rc=r.returncode, out=(r.stdout + r.stderr)[-160:])
            m = re.search(r"[Gg]ateway\W+(on|off|true|false|0|1)\b", info)
            if r.returncode != 0 or m is None or m.group(1) in ("on", "true", "1"):
                raise Abort("%s: guard failed or gateway unknown/already ON" % n.call)
        st = http(self.n90.base, "/rmstatus")
        self.rm90_before = int(st["on"]) if isinstance(st, dict) and "on" in st else None
        json.dump({"rmstatus": st, "rm_on": self.rm90_before}, open(os.path.join(self.dir, "rm90_before.json"), "w"))
        self.ev("rm_state_before", self.n90.call, result=str(self.rm90_before))
        if self.rm90_before is None:
            raise Abort("cannot read DK5EN-90 RM state from /rmstatus -> cannot restore it")
        http(self.n90.base, "/setparam/?rm=on")
        time.sleep(2)
        if http(self.n90.base, "/rmstatus").get("on") != 1:
            raise Abort("DK5EN-90: RM receive did not turn on via /setparam/?rm=on")

    def start_mock(self) -> None:
        global MOCK_IP
        MOCK_IP = socket.gethostbyname(MOCK_SSH)
        ssh("mkdir -p ~/%s && pkill -f '[m]eshcom_server.py --port 1990'; true" % MOCK_DIR)
        subprocess.run(["scp", "-q", os.path.join(REPO, "tools/mock/meshcom_server.py"), "%s:%s/" % (MOCK_SSH, MOCK_DIR)], check=True, timeout=30)
        ssh("rm -rf ~/%s/cap" % MOCK_DIR)
        self.mock = subprocess.Popen(["ssh", "-o", "BatchMode=yes", MOCK_SSH, "cd ~/%s && exec python3 -u meshcom_server.py --port 1990 --capture-dir cap --verbose" % MOCK_DIR],
                                     stdout=open(os.path.join(self.dir, "mock.log"), "a"), stderr=subprocess.STDOUT)
        time.sleep(4)
        if self.mock.poll() is not None or "event=listen" not in self.mock_text():
            raise Abort("mock server on %s did not start: %s" % (MOCK_SSH, self.mock_text()[-200:]))

    def stop_mock(self) -> None:
        try:
            ssh("pkill -INT -f '[m]eshcom_server.py --port 1990'; sleep 3; pgrep -f '[m]eshcom_server.py --port 1990' || true", timeout=30)
            subprocess.run(["scp", "-q", "-r", "%s:%s/cap" % (MOCK_SSH, MOCK_DIR), os.path.join(self.dir, "mock")], timeout=60)
        except Exception as e:  # noqa: BLE001
            print("stop_mock:", e)
        if self.mock:
            try:
                self.mock.wait(5)
            except subprocess.TimeoutExpired:
                self.mock.kill()

    def p_setup(self) -> None:
        self.start_mock()
        for n in self.nodes:
            p = n.pos(); n.con("--srvip " + MOCK_IP); self.srv_set = True
            if not n.wait(r"\[SRVIP\];%s;set" % re.escape(MOCK_IP), p, 15):
                raise Abort(n.call + ": no [SRVIP];set -> gateway NOT enabled")
            # Heltec prints only `;set` plus (after DNS) `[WIFI];dns;..;ip;<ip>` / `BENCH srvip override`; the hard proof is KEEP at the mock.
            pat = r"\[SRVIP\];%s;applied" % re.escape(MOCK_IP) if n is self.n90 else r"\[WIFI\](?:;dns;[^\n]*;ip;|\.\.\.BENCH srvip override -> )%s" % re.escape(MOCK_IP)
            if re.search(r"\[SRVIP\];note", n.text(p)):
                raise Abort(n.call + ": srvip note (WiFi/ETH not up) -> gateway NOT enabled")
            if not n.wait(pat, p, 30):
                if n is self.n90:
                    raise Abort(n.call + ": srvip not applied -> gateway NOT enabled")
                print("note: no DNS/applied line on %s; relying on the KEEP check" % n.call, flush=True)
            self.ev("srvip", n.call, result="set/applied", mock=MOCK_IP)
        for n in self.nodes:
            n.con("--udplog on"); self.udplog_on = True
            if self.a.loradebug:
                n.con("--loradebug on")
        self.nap(2)
        self.guard = gwguard.BootGuard(self.nodes, self.gw_off_sender)  # tails from NOW, before the first --gateway on
        self.guard.start()
        for n in self.nodes:
            n.con("--gateway on"); self.nap(3)
        t0 = time.time()
        while not all(gwguard.keep_seen(self.mock_text(), n.call, n.host) for n in self.nodes):
            if time.time() - t0 > 90:
                seen = {n.call: gwguard.keep_seen(self.mock_text(), n.call, n.host) for n in self.nodes}
                raise Abort("mock saw no KEEP (callsign + node IP) from both nodes within 90 s of gateway on: %s" % seen)
            self.nap(2)
        self.ev("gateway_registered", "both", result="ok")

    def gw_off_sender(self, n: Node) -> bool:
        return gwguard.gateway_off_sender(n, http, lambda x: x.con("--gateway off"))

    def rm_post(self, src: Node, dst: Node, cmd: str, args: str = "", pw: Optional[str] = None, force: bool = False) -> Tuple[Optional[Dict[str, Any]], float]:
        """POST /rmsend until accepted. Returns (refusal result or None, t0 of the accepted send)."""
        guard_call(dst.call)
        if cmd in ("radio", "pos") and args:
            raise Abort("bench rule: no radio/pos writes")
        if cmd == "txpower":
            try:
                dbm = int(args.strip())
            except ValueError:
                raise Abort("bench rule: txpower argument %r is not an integer" % args) from None
            if dbm > 2:
                raise Abort("bench rule: no txpower write above 2 dBm")
        body = "dst=%s&pw=%s&cmd=%s&args=%s" % (dst.call, q(pw or dst.pw), cmd, q(args)) + ("&force=1" if force else "")
        j: Any = {}
        for _ in range(25):
            j = http(src.base, "/rmsend", body)
            if isinstance(j, dict) and j.get("ok"):
                return None, time.time()
            if isinstance(j, dict) and j.get("err") in ("busy", "limit", "cooldown"):
                self.nap(max(1, int(j.get("retry", 3))) + 1); continue
            break
        return {"result": "refused", "reply": json.dumps(j), "lat": 0.0, "ver": None, "t0": time.time()}, time.time()

    def rm_wait(self, src: Node, dst: Node, cmd: str, args: str, t0: float, wait: int = RM_WAIT) -> Dict[str, Any]:
        want = (cmd + " " + args).strip()
        while time.time() - t0 < wait:
            self.nap(2)
            for e in http(src.base, "/rmstatus").get("sent", []):
                if e["dst"] == dst.call and e["cmd"] == want and e["ago"] <= time.time() - t0 + 5 and e["st"] in ("ok", "err", "unverified", "noanswer"):
                    return {"result": e["st"], "reply": e["reply"], "lat": round(time.time() - t0, 1), "ver": e["ver"], "t0": t0}
        return {"result": "timeout", "reply": "", "lat": round(time.time() - t0, 1), "ver": None, "t0": t0}

    def rm_send(self, src: Node, dst: Node, cmd: str, args: str = "", pw: Optional[str] = None, force: bool = False, wait: int = RM_WAIT) -> Dict[str, Any]:
        ref, t0 = self.rm_post(src, dst, cmd, args, pw, force)
        if ref is not None:
            return ref
        return self.rm_wait(src, dst, cmd, args, t0, wait)

    def rm_case(self, src: Node, dst: Node, cmd: str, args: str = "", tag: str = "") -> Dict[str, Any]:
        b = http(dst.base, "/rmstatus"); r = self.rm_send(src, dst, cmd, args); a = http(dst.base, "/rmstatus")
        self.ev("rm:" + (cmd + " " + args).strip() + tag, "%s>%s" % (src.call[6:], dst.call[6:]), result=r["result"], lat=r["lat"], ver=r["ver"],
                reply=r["reply"], t_send=r["t0"], ok_d=a["ok"] - b["ok"], rej_d=a["rej"] - b["rej"])
        self.nap(6)
        return r

    def p_rm(self) -> None:
        for src, dst in ((self.n1, self.n90), (self.n90, self.n1)):
            d = "%s>%s" % (src.call[6:], dst.call[6:])
            for c in RM_READ:
                cmd, _, args = c.replace("{PEER}", src.call).partition(" ")
                self.rm_case(src, dst, cmd, args)
            for cmd, key in (("name", "n"), ("atxt", "[at]")):
                r = self.rm_case(src, dst, cmd); m = re.match(r"^ok\s+%s=(.*)$" % key, r["reply"] or "")
                if m:
                    self.rm_case(src, dst, cmd, m.group(1), tag=" (same)")
                    self.ev("verify_restore:" + cmd, d, result="same" if self.rm_case(src, dst, cmd)["reply"] == r["reply"] else "DIFFERENT")
                else:
                    self.ev("rm:%s write" % cmd, d, result="skipped", reply="read format unknown: " + (r["reply"] or ""))
            r = self.rm_case(src, dst, "maxhop"); m = re.search(r"(\d+)\s*$", r["reply"] or "")
            if m:
                old = int(m.group(1))
                self.rm_case(src, dst, "maxhop", str(old - 1 if old > 1 else old + 1), tag=" (temp)")
                self.rm_case(src, dst, "maxhop", str(old), tag=" (restore)")
                self.ev("verify_restore:maxhop", d, result="same" if self.rm_case(src, dst, "maxhop")["reply"] == r["reply"] else "DIFFERENT")

    def p_refused(self) -> None:
        """RM-STRIKE / sender clamp (txpower-clamp plan, waves 1-2): DK5EN-1 manages the RAK (floor 2 dBm).
        Every value sent is <= 2 dBm, so even a wrongly accepted command stays inside the bench rule."""
        src, dst, d = self.n1, self.n90, "1>90"
        # a fresh sender boot empties its learnt range table: the first txpower 1 must go on air
        src.con("--reboot")
        self.nap(25)
        t0 = time.time()
        while not names_call(src.base, src.call):
            if time.time() - t0 > 90:
                raise Abort("DK5EN-1 web page not back 90 s after --reboot")
            self.nap(3)
        self.nap(5)
        p90 = dst.pos(); b = http(dst.base, "/rmstatus")
        r = self.rm_send(src, dst, "txpower", "1")
        a = http(dst.base, "/rmstatus")
        m = re.search(r"\[RM\];refused;ctr;(\d+)", dst.text(p90))
        ok = r["result"] == "err" and r["ver"] and r["reply"] == "err range 2..22" and m is not None and not a.get("lock")
        self.ev("refused:on-air txpower 1", d, result="PASS" if ok else "FAIL", reply=r["reply"], ver=r["ver"],
                marker=m.group(0) if m else None, lock=a.get("lock"), ok_d=a["ok"] - b["ok"], rej_d=a["rej"] - b["rej"])
        self.nap(12)
        j = http(src.base, "/rmsend", "dst=%s&pw=%s&cmd=txpower&args=1" % (dst.call, q(dst.pw)))
        ok = isinstance(j, dict) and not j.get("ok") and j.get("err") == "range" and j.get("range") == "2..22"
        self.ev("refused:local txpower 1", d, result="PASS" if ok else "FAIL", reply=json.dumps(j))
        r = self.rm_send(src, dst, "radio")
        ok = r["result"] == "ok" and re.search(r"\bp=-?\d+/2/22$", r["reply"] or "") is not None
        self.ev("refused:radio p=", d, result="PASS" if ok else "FAIL", reply=r["reply"], ver=r["ver"])
        self.nap(12)
        r = self.rm_send(src, dst, "txpower", "2")
        ok = r["result"] == "ok" and r["reply"] == "ok txpower=2"
        self.ev("refused:in-range txpower 2", d, result="PASS" if ok else "FAIL", reply=r["reply"], ver=r["ver"])

    # ------------------------------------------------------------------ airgap / debug switches (INSTRUMENT image, RAM only)
    def airgap(self, n: Node, state: str, tries: int = 3) -> bool:
        if state == "on":
            self.airgap_used = True  # before the first `on`: teardown must clear it even if we abort in between
        for _ in range(tries):
            p = n.pos(); n.con("--airgap " + state)
            if n.wait(r"\[AIRGAP\];%s\b" % state, p, 8):
                self.ev("airgap", n.call, result=state)
                return True
        return False

    def airgap_probe(self) -> None:
        for n in self.nodes:
            p = n.pos(); n.con("--airgap")
            if not n.wait(r"\[AIRGAP\];(?:on|off)\b", p, 10):
                raise Abort("%s: --airgap not answered (stock image or wrong command?); phase repeat needs the INSTRUMENT image on both nodes" % n.call)
            if re.search(r"\[AIRGAP\];on\b", n.text(p)):
                raise Abort("%s: airgap already on at start; refusing (power-cycle or send --airgap off by hand)" % n.call)

    def dbg_enable(self) -> None:
        """RAM-only log switches: --setinfo on (NEW-TXT/TX-LoRa lines), --setretx on ([RETX] PNRETRY lines). Cleared in teardown."""
        if self.dbg_on:
            return
        self.dbg_on = True
        for n in self.nodes:
            n.con("--setinfo on"); n.con("--setretx on")
        self.nap(2)

    def nap_until(self, t: float) -> None:
        self.nap(max(0.0, t - time.time()))

    def wait_copy1(self, src: Node, dst: Node, p: int, t0: float) -> Tuple[Optional[int], str]:
        """Return (ctr, marker) once copy 1 has left the sender. Marker `TX-LoRa` = the sender's own TX line (needs --setinfo on);
        fallback `waitNs` = COPY1_WAIT seconds after /rmsend returned ok (no reliable marker seen)."""
        if not src.wait(r"\[RM\];send;%s;ctr;\d+" % re.escape(dst.call), p, 15):
            return None, "no [RM];send"
        ctr = int(re.findall(r"\[RM\];send;%s;ctr;(\d+)" % re.escape(dst.call), src.text(p))[-1])
        if src.wait(r"TX-Lo[Rr]a[^\n]*RM1 %d " % ctr, p, COPY1_WAIT):
            return ctr, "TX-LoRa"
        self.nap_until(t0 + COPY1_WAIT)
        return ctr, "wait%ds" % COPY1_WAIT

    def p_repeat(self) -> None:
        """RM1 repeat ladder (txpower-clamp plan, wave 3). LoRa only, `status` DK5EN-1 > DK5EN-90, nothing written, no TX power change.
        The verdicts (one execution, cached answer, stop, no stray copy) come from gwanalyze, which reads the events and the logs."""
        if "setup" in self.a.phases:
            raise Abort("phase repeat is LoRa only: do not combine it with setup (gateway on)")
        self.airgap_probe()
        for n in self.nodes:
            if not self.airgap(n, "off"):
                raise Abort(n.call + ": --airgap off not confirmed before the test")
        self.dbg_enable()
        src, dst, d = self.n1, self.n90, "1>90"
        # (a) lost command: the target is deaf while copy 1 is on air; copy 2 (~42 s) must execute and verify
        self.airgap(dst, "on") or self.fail_airgap(dst)
        p1 = src.pos(); ref, t0 = self.rm_post(src, dst, "status")
        if ref is not None:
            raise Abort("repeat a: /rmsend refused: %s" % ref["reply"])
        ctr, marker = self.wait_copy1(src, dst, p1, t0)
        self.nap_until(time.time() + GAP_OFF)
        self.airgap(dst, "off") or self.fail_airgap(dst)
        t_off = time.time() - t0
        r = self.rm_wait(src, dst, "status", "", t0)
        self.nap_until(t0 + LADDER_END)
        self.ev("repeat:lost-command", d, result=r["result"], lat=r["lat"], ver=r["ver"], reply=r["reply"], t_send=t0, ctr=ctr, copy1_marker=marker,
                airgap_off_at=round(t_off, 1), expect={"min_repeats": 1, "exec": 1, "max_cached": 0})
        self.nap(10)
        # (b) lost reply: the sender is deaf while the target answers; copy 2 must be answered from the reply cache
        p1 = src.pos(); ref, t0 = self.rm_post(src, dst, "status")
        if ref is not None:
            raise Abort("repeat b: /rmsend refused: %s" % ref["reply"])
        ctr, marker = self.wait_copy1(src, dst, p1, t0)
        t1 = time.time()
        self.airgap(src, "on") or self.fail_airgap(src)
        raced = ctr is not None and re.search(r"\[RM\];reply;%s;ctr;%d;verified;1" % (re.escape(dst.call), ctr), src.text(p1)) is not None
        self.nap_until(t1 + REPLY_GAP_OFF)
        self.airgap(src, "off") or self.fail_airgap(src)
        r = self.rm_wait(src, dst, "status", "", t0)
        self.nap_until(t0 + LADDER_END)
        self.ev("repeat:lost-reply", d, result=r["result"], lat=r["lat"], ver=r["ver"], reply=r["reply"], t_send=t0, ctr=ctr, copy1_marker=marker,
                reply_beat_airgap=raced, expect={"min_repeats": 1, "exec": 1, "min_cached": 1})

    def fail_airgap(self, n: Node) -> None:
        raise Abort("%s: --airgap change not confirmed by [AIRGAP];..." % n.call)

    def junk(self, src: Node, dst: Node, n: int) -> None:
        """Wrong-tag RM frame as a plain DM (the sender policy would refuse a third wrong-password send by design)."""
        hwm = int(http(dst.base, "/rmstatus")["hwm"])
        http(src.base, "/?sendmessage&tocall=%s&message=%s" % (guard_call(dst.call), q("RM1 %d status 0123456789abcde%d" % (hwm + 50 + n, n))))

    def p_neg(self) -> None:
        """Wrong-tag RM1 frames as plain DMs. Since the RM1 repeat ladder every such DM goes out 4x (copy 1 + up to 3 repeats
        ~42 s apart) and nothing ever answers it, so the ladder runs to its end (LADDER_END, past 135 s). Expectations are on the
        target's strike/lock state, not on raw reject counts (each copy, and each RF/server delivery, is a `[RM];reject` line and
        raises `rej`; the target's "same guess = one strike" rule dedups against every strike counted in the window, at most 2):
          x1: ONE junk frame, full ladder: all copies are one guess -> rej_d >= 1 and NO lock.
          x3: three DISTINCT junk frames (other ctr and tag) 15 s apart, inside the 90 s strike window -> lock, rej_d >= 3.
              Repeat copies of the earlier frames add no strike (same guess) and are `lockout` once locked. The check waits the full
              ladder after the last send, so no repeat is still pending when the lock is read, reset or tested (lock lasts 300 s)."""
        for src, dst in ((self.n1, self.n90), (self.n90, self.n1)):
            d = "%s>%s" % (src.call[6:], dst.call[6:])
            http(dst.base, "/rmpasswd", "act=set&pw=" + q(dst.pw)); self.nap(2)  # clean reject table
            b = http(dst.base, "/rmstatus"); t0 = time.time(); self.junk(src, dst, 0); self.nap_until(t0 + LADDER_END); a = http(dst.base, "/rmstatus")
            rd = a["rej"] - b["rej"]
            self.ev("neg:junk x1", d, result="ok" if rd >= 1 and not a["lock"] else "UNEXPECTED", rej_d=rd, lock=a["lock"], t_send=t0, expect="rej_d>=1, no lock")
            http(dst.base, "/rmpasswd", "act=set&pw=" + q(dst.pw)); self.nap(2)  # fresh strike window for the lock test
            b = http(dst.base, "/rmstatus"); t0 = time.time(); t_last = t0
            for k in (1, 2, 3):
                t_last = time.time(); self.junk(src, dst, k)
                if k < 3:
                    self.nap(15)
            self.nap_until(t_last + LADDER_END); a = http(dst.base, "/rmstatus")
            rd = a["rej"] - b["rej"]
            self.ev("neg:junk x3 lock", d, result="locked" if a["lock"] and rd >= 3 else "UNEXPECTED", rej_d=rd, lock=a["lock"], lockS=a["lockS"], t_send=t0, expect="lock, rej_d>=3")
            r = self.rm_send(src, dst, "status"); a2 = http(dst.base, "/rmstatus")
            self.ev("neg:valid cmd while locked", d, result=r["result"], reply=r["reply"], ok_d=a2["ok"] - a["ok"], t_send=r["t0"])
            http(dst.base, "/rmpasswd", "act=set&pw=" + q(dst.pw)); self.nap(2)
            self.ev("neg:unlock", "local@" + dst.call[6:], result="ok" if not http(dst.base, "/rmstatus")["lock"] else "STILL locked")
            r = self.rm_send(src, dst, "sync")
            self.ev("neg:sync after unlock", d, result=r["result"], reply=r["reply"], t_send=r["t0"])
        self.ev("neg:per-sender", "-", result="skipped: needs a third sender callsign (covered in the RF round)")
        self.ev("neg:replay", "-", result="covered by the double delivery: every command arrives twice with the same counter and tag")

    def p_plain(self) -> None:
        for src, dst in ((self.n1, self.n90), (self.n90, self.n1)):
            for kind, to in (("dm", dst.call), ("grp", "TEST")):
                tok = "gwb%s-%s%s" % (time.strftime("%H%M%S"), kind, src.call[6:]); t0 = time.time()
                http(src.base, "/?sendmessage&tocall=%s&message=%s" % (guard_call(to), q(tok)))
                self.ev("plain:" + kind, "%s>%s" % (src.call[6:], to), result="sent", token=tok, t_send=t0, t_end=t0 + 25, src=src.call, to=to)
                self.nap(25)

    def confirm_gateway_off(self, n: Node) -> bool:
        for attempt in range(3):
            p = n.pos(); n.con("--info"); time.sleep(6)
            info = gwguard.parse_info(n.text(p))
            if info["gateway"] == "off" and info["call"] in (None, n.call):
                return True
            n.con("--gateway off")
            try:
                http(n.base, gwguard.GW_OFF_PATH)
            except Exception:  # noqa: BLE001
                pass
            time.sleep(4)
        return False

    def teardown(self) -> None:
        self.raise_trip = False
        if self.airgap_used or "repeat" in self.a.phases:
            for n in self.nodes:
                if n.cmd:
                    ok = False
                    for _ in range(3):
                        p = n.pos(); n.con("--airgap off")
                        if n.wait(r"\[AIRGAP\];off\b", p, 8):
                            ok = True
                            break
                    print(("AIRGAP OFF CONFIRMED " if ok else "AIRGAP STATE UNCONFIRMED ") + n.call, flush=True)
        if self.dbg_on:
            for n in self.nodes:
                if n.cmd:
                    n.con("--setretx off"); n.con("--setinfo off")
        if self.guard:
            self.guard.stop_ev.set(); self.guard.join_senders(125)
        for n in self.nodes:
            if n.cmd:
                n.con("--gateway off")
        time.sleep(8)
        results = {}
        for n in self.nodes:
            results[n.call] = bool(n.cmd) and self.confirm_gateway_off(n)
            print(("GATEWAY OFF CONFIRMED " if results[n.call] else "GATEWAY STATE UNCONFIRMED ") + n.call, flush=True)
        for n in self.nodes:
            if n.cmd and self.srv_set and results.get(n.call):  # never clear srvip while the gateway may still be on
                n.con("--srvip 0.0.0.0")
            if n.cmd and self.udplog_on:
                n.con("--udplog off")
                if self.a.loradebug:
                    n.con("--loradebug off")
        time.sleep(5)
        try:
            if self.rm90_before is not None:
                http(self.n90.base, "/setparam/?rm=" + ("on" if self.rm90_before else "off"))
        except Exception as e:  # noqa: BLE001
            print("teardown rm90 restore:", e)
        for n in self.nodes:
            if n.proc:
                n.proc.send_signal(signal.SIGTERM)
        if self.mock:
            self.stop_mock()

    def run(self) -> None:
        os.makedirs(self.dir, exist_ok=True)
        try:
            self.start_loggers()
            for name in PHASES[:-1]:
                if name in self.a.phases:
                    print("== phase", name, flush=True)
                    if name != "guard" and self.a.lora_only:
                        self.dbg_enable()
                    getattr(self, "p_" + name)()
        except Abort as e:
            print("ABORT:", e); self.ev("abort", "-", result=str(e))
        finally:
            print("== teardown"); self.teardown()
        print("run dir:", self.dir)


def print_plan(a: argparse.Namespace) -> None:
    print("DRY RUN (no I/O): phases", a.phases, "+ teardown; RAK host", a.rak_host)
    if a.lora_only:
        print("mode: LoRa only: gateway OFF on both nodes (guard aborts otherwise); no mock, no --srvip, no setup phase; "
              "--setinfo on / --setretx on (RAM only) for the TX and PNRETRY log lines")
    else:
        print("mode: both nodes as gateways against the mock on", MOCK_SSH)
    if "rm" in a.phases:
        print("rm:", RM_READ)
    if "repeat" in a.phases:
        print("repeat plan (INSTRUMENT image on both nodes, otherwise abort; RM1 `status` DK5EN-1 > DK5EN-90, no write, no TX power change):")
        print("  probe `--airgap` on both nodes; both must answer [AIRGAP];off")
        print("  a lost command : --airgap on at DK5EN-90; /rmsend status; wait for copy 1 on air (sender TX-LoRa line, else %d s after /rmsend);"
              % COPY1_WAIT)
        print("                   --airgap off at DK5EN-90 %d s after copy 1; expect copy 2 (~42 s) executes ([RM];ok once), reply verified, no copy after the stop; observe to %d s" % (GAP_OFF, LADDER_END))
        print("  b lost reply   : /rmsend status; wait for copy 1 on air; --airgap on at DK5EN-1; --airgap off at DK5EN-1 %d s after copy 1;" % REPLY_GAP_OFF)
        print("                   expect copy 2 answered from the cache ([RM];cached, no second [RM];ok) and verified; observe to %d s" % LADDER_END)
        print("  teardown       : --airgap off on both nodes, wait for [AIRGAP];off (always), then --setretx off / --setinfo off")
        print("  analysis       : gwanalyze.py <run dir> (FAIL lines and exit 1 on second execution, strike, second verified row, stray repeat)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--phases", default=None, help="comma list of guard,setup,rm,neg,plain,refused,repeat; teardown always runs (default %s; with --lora-only %s). "
                    "refused/repeat: LoRa only, run without setup; repeat needs the INSTRUMENT image (--airgap)" % (DEFAULT_PHASES, LORA_ONLY_PHASES))
    ap.add_argument("--lora-only", action="store_true", help="gateway OFF on both nodes: no mock, no --srvip, no setup phase (guard confirms gateway off)")
    ap.add_argument("--run-dir", default=None)
    ap.add_argument("--rak-port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--rak-host", default="192.168.68.73", help="RAK web address (DHCP, no mDNS); the page must name DK5EN-90")
    ap.add_argument("--heltec-host", default=None, help="default: resolve dk5en-1.local")
    ap.add_argument("--loradebug", action="store_true", help="also --loradebug on (RX-UDP line with msg id; noisy)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    a.phases = (a.phases or (LORA_ONLY_PHASES if a.lora_only else DEFAULT_PHASES)).split(",")
    bad = [p for p in a.phases if p not in PHASES[:-1]]
    if bad:
        ap.error("unknown phase(s): %s" % ",".join(bad))
    if "repeat" in a.phases:
        a.lora_only = True  # repeat is LoRa only
    if "guard" not in a.phases:
        # every phase talks to the nodes: identity_guard and the gateway-off confirmation must never be skipped
        a.phases = ["guard"] + a.phases
        print("note: phase guard added (identity_guard + gateway-off confirmation are mandatory whenever a phase talks to the nodes)", flush=True)
    if a.lora_only and "setup" in a.phases:
        a.phases = [p for p in a.phases if p != "setup"]
        print("note: LoRa only: phase setup (mock, --srvip, gateway on) skipped", flush=True)
    if a.dry_run:
        print_plan(a)
        return
    Run(a).run()


if __name__ == "__main__":
    main()
