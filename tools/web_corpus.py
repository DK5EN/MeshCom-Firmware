#!/usr/bin/env python3
"""Builds the request corpus of the web differential rig (FORMAL-02, web server wave A step 0).

Output: test/test_web_diff/corpus/<name>.req (raw request bytes, CRLF as sent on the wire) and
corpus/INDEX.txt (one name per line, sorted). Deterministic: the same repository state gives the same
bytes; `--check` regenerates in memory and fails when the checked-in corpus differs.

Sources (docs/web-server-seams-20261127.md 7.1), the name prefix says which:
  cap  the real capture tools/bench/runs/harness/webcap-dk5en14-20261127.jsonl (raw_b64); only normalised:
       the bench node's IP address is replaced by 192.0.2.10 (Host, Referer), cookie values by a placeholder
  js   URL templates of src/web_functions/web/*.js, instantiated with the setparam names of
       src/web_functions/web_setup.cpp; plus the POST bodies the Remote page and the config upload send
  doc  the command lines of src/web_functions/Web-API_documentation.txt
  grd  the literal request/header matrices of test/test_web_guard/test_main.cpp
  mut  malformed and edge variants (HTTP/1.0, no version, missing '=', huge Content-Length, Referer that
       holds a route, percent edge cases, truncated heads, long lines, NUL bytes, bare LF)

A name ending in `__closed` means: the fake peer closes its side after the last byte (the head loop sees
connected() == false); otherwise the peer stays open and a head that never ends runs into the 2 s deadline.

Session placeholder: a request that carries `Cookie: mcs=` + SESSION_PLACEHOLDER gets the real session token
of the rig in the profile that has a logged-in session (the placeholder has the length of a token).

Usage: python3 tools/web_corpus.py [--check] [--out DIR] [--stats]
"""
from __future__ import annotations

import argparse
import base64
import json
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional

ROOT = Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "tools/bench/runs/harness/webcap-dk5en14-20261127.jsonl"
JS_DIR = ROOT / "src/web_functions/web"
DOC = ROOT / "src/web_functions/Web-API_documentation.txt"
GUARD_TEST = ROOT / "test/test_web_guard/test_main.cpp"
SETUP_CPP = ROOT / "src/web_functions/web_setup.cpp"
OUT_DIR = ROOT / "test/test_web_diff/corpus"

HOST = "192.0.2.10"
CAP_HOST = "192.168.68.77"  # the bench node of the capture
SESSION_PLACEHOLDER = "aaaaaaaabbbbbbbbccccccccdddddddd"  # 32 chars, same length as a session token
CRLF = "\r\n"

BROWSER_UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) "
              "Chrome/158.0.0.0 Safari/537.36")

Corpus = Dict[str, bytes]


def req(line: str, headers: Iterable[str] = (), body: bytes = b"") -> bytes:
    head = line + CRLF + "".join(h + CRLF for h in headers) + CRLF
    return head.encode("latin-1") + body


def browser_headers(host: str = HOST, xmc: bool = False, referer: Optional[str] = None,
                    fetch_site: str = "same-origin") -> List[str]:
    h = [
        "Host: " + host,
        "Connection: keep-alive",
        'sec-ch-ua-platform: "macOS"',
        "User-Agent: " + BROWSER_UA,
        "Accept: */*",
        "Sec-Fetch-Site: " + fetch_site,
        "Sec-Fetch-Mode: cors",
        "Sec-Fetch-Dest: empty",
        "Accept-Encoding: gzip, deflate, br, zstd",
        "Accept-Language: de,en;q=0.9,en-US;q=0.8",
    ]
    if xmc:
        h.append("X-MC: 1")
    if referer is not None:
        h.append("Referer: " + referer)
    return h


def curl_headers(host: str = HOST, xmc: bool = True) -> List[str]:
    h = ["Host: " + host, "User-Agent: curl/8.7.1", "Accept: */*"]
    if xmc:
        h.append("X-MC: 1")
    return h


def slug(s: str, limit: int = 40) -> str:
    out = re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-").lower()
    return out[:limit].rstrip("-") or "x"


class Builder:
    def __init__(self) -> None:
        self.items: Corpus = {}
        self.by_source: Dict[str, int] = {}
        self._seen: Dict[bytes, str] = {}

    def add(self, source: str, label: str, data: bytes, closed: bool = False) -> None:
        if data in self._seen and not closed:
            return  # identical bytes under another label: one entry is enough
        n = self.by_source.get(source, 0) + 1
        self.by_source[source] = n
        name = "%s-%03d-%s%s" % (source, n, slug(label), "__closed" if closed else "")
        self.items[name] = data
        self._seen.setdefault(data, name)


# ---------------------------------------------------------------- (a) capture

def from_capture(b: Builder) -> None:
    for i, line in enumerate(CAPTURE.read_text().splitlines()):
        if not line.strip():
            continue
        d = json.loads(line)
        raw = base64.b64decode(d["raw_b64"])
        raw = raw.replace(CAP_HOST.encode(), HOST.encode())
        raw = re.sub(rb"(?im)^(cookie:[^\r\n]*mcs=)[0-9a-f]{32}", rb"\1" + SESSION_PLACEHOLDER.encode(), raw)
        # keep every capture entry, also exact duplicates: the capture is the ground truth of a session
        n = b.by_source.get("cap", 0) + 1
        b.by_source["cap"] = n
        b.items["cap-%03d-%s" % (n, slug(d["line"].split(" ")[1]) if d["line"].split(" ")[1] != "/" else "root")] = raw


# ---------------------------------------------------------------- (b) JS call sites

JS_URL = re.compile(r"""(?:['"])((?:/(?:setparam|getparam|callfunction)/\?|/rm[a-z]+|/pin[a-z]+|/gpiostatus|/austatus|"""
                    r"""/config(?:\.json)?|\?page=|\?sendmessage|\?getmessages|\?login|\?logout)[^'"]*)['"]""")


def js_templates() -> List[str]:
    out: List[str] = []
    for p in sorted(JS_DIR.glob("*.js")):
        for m in JS_URL.finditer(p.read_text(errors="replace")):
            t = m.group(1)
            if t not in out:
                out.append(t)
    return out


def setup_param_names() -> List[str]:
    names = set()
    txt = SETUP_CPP.read_text(errors="replace")
    for m in re.finditer(r'paramName\.equals\("([A-Za-z0-9_]+)"\)', txt):
        names.add(m.group(1))
    for m in re.finditer(r'\bWSP_[A-Z_0-9]+\s*\(\s*"([A-Za-z0-9_]+)"', txt):  # the wsParams table rows
        names.add(m.group(1))
    for p in sorted(JS_DIR.glob("*.js")):
        for m in re.finditer(r"/setparam/\?([A-Za-z0-9_]+)=", p.read_text(errors="replace")):
            names.add(m.group(1))
    return sorted(names)


PAGES = ["info", "position", "wx", "setup", "messages", "mailbox", "rxlog", "path", "mheard", "neighbours",
         "spectrum", "mcp23017", "pins", "remote", "nosuchpage", ""]
GETS = ["/pinstatus", "/pinassign", "/gpiostatus", "/rmstatus", "/rmnodes", "/rmheard", "/austatus",
        "/config.json", "/?getmessages", "/screen.oled", "/screen.rgb565", "/mc.css", "/mc.js", "/mcrm.js",
        "/mcau.js", "/mc.css?v=c9cedb8b", "/mc.js?v=70ea8e19", "/", "/favicon.ico", "/ota/state"]
VALUES = ["on", "off", "1", "0", "5", "-3.5", "AB1CDE-12", "x y", "%E4%F6%FC", ""]
CALLS = ["sendpos", "reboot", "otaupdate", "updcheck", "updinstall", "updapply", "mboxpurge=all", "mboxpurge=3",
         "mboxdeliver=2", "nosuchfunction", "sendpos=1", "a%22b=c"]
MESSAGES = [
    "/?sendmessage&tocall=9999&message=hello",
    "/?sendmessage&tocall=9999&message=Gr%C3%BC%C3%9Fe%20aus%20dem%20Test",
    "/?sendmessage&tocall=&message=ohne%20Ziel",
    "/?sendmessage&tocall=dk5en-99&message=%20%20pad%20%20",
    "/?sendmessage&tocall=9999&message=",
    "/?sendmessage&tocall=9999",
    "/?sendmessage&message=x",
    "/?sendmessage",
    "/?sendmessage&tocall=9999&message=a&b=c&message=d",
    "/?sendmessage&tocall=9999&message=" + "A" * 200,
    "/?sendmessage&tocall=9999&message=" + "%C3%A4" * 90,
    "/?sendmessage&tocall=9999&message=x%0D%0Ay%00z%22q",
    "/?sendmessage&tocall=9999&message=a+b",
]


def from_js(b: Builder) -> None:
    tmpl = js_templates()
    # every template literal as the page would request it with an empty tail
    for t in tmpl:
        path = t if t.startswith("/") else "/" + t
        b.add("js", "tmpl-" + t, req("GET " + path + " HTTP/1.1", browser_headers(xmc=True)))
    for pg in PAGES:
        b.add("js", "page-" + pg, req("GET /?page=%s HTTP/1.1" % pg, browser_headers()))
        b.add("js", "pageq-" + pg, req("GET ?page=%s HTTP/1.1" % pg, browser_headers()))
    for g in GETS:
        b.add("js", "get-" + g, req("GET %s HTTP/1.1" % g, browser_headers()))
    for n in setup_param_names():
        b.add("js", "set-" + n, req("GET /setparam/?%s=on HTTP/1.1" % n, browser_headers(xmc=True)))
        b.add("js", "get-" + n, req("GET /getparam/?%s HTTP/1.1" % n, browser_headers()))
    for v in VALUES:
        b.add("js", "setval", req("GET /setparam/?atxt=%s HTTP/1.1" % v, browser_headers(xmc=True)))
    for c in CALLS:
        b.add("js", "call-" + c, req("GET /callfunction/?%s HTTP/1.1" % c, browser_headers(xmc=True)))
    for m in MESSAGES:
        b.add("js", "msg", req("GET %s HTTP/1.1" % m, browser_headers(xmc=True)))
    # POST bodies of the Remote page (mcrm.js rmPost) and the settings page (mc.js rmPost, uploadconfig)
    posts = [
        ("/rmsend", "dst=DK5EN-99&pw=secret&cmd=mh&args=5"),
        ("/rmsend", "slot=1&cmd=txpower&args=2&call=DK5EN-99"),
        ("/rmsend", "slot=1&cmd=reboot&args=&force=1"),
        ("/rmsend", "dst=DK5EN-99&pw=a%26b%3Dc&cmd=info&args=%20x"),
        ("/rmsend", ""),
        ("/rmpasswd", "pw=newsecret"),
        ("/rmpasswd", "clear=1"),
        ("/rmwebpwd", "pw=guipw"),
        ("/rmwebpwd", "clear=1"),
        ("/rmnodes", "op=save&slot=0&call=DK5EN-99&pw=secret"),
        ("/rmnodes", "op=delete&slot=0"),
        ("/rmnodes", "op=forget&slot=2"),
    ]
    for path, body in posts:
        bb = body.encode()
        hdr = browser_headers(xmc=True, referer="http://%s/" % HOST) + [
            "Content-Type: application/x-www-form-urlencoded", "Content-Length: %d" % len(bb),
            "Origin: http://%s" % HOST]
        b.add("js", "post" + path, req("POST %s HTTP/1.1" % path, hdr, bb))
    cfg = b'{"node_call":"DK5EN-99","node_atxt":"test","tx_power":2}'
    for path in ("/config", "/config.json"):
        hdr = browser_headers(xmc=True) + ["Content-Type: application/json", "Content-Length: %d" % len(cfg)]
        b.add("js", "post-cfg" + path, req("POST %s HTTP/1.1" % path, hdr, cfg))
    big = b"x" * 6200
    b.add("js", "post-cfg-big", req("POST /config HTTP/1.1", browser_headers(xmc=True) + ["Content-Length: 6200"], big))
    short = b"y" * 10
    b.add("js", "post-cfg-short", req("POST /config HTTP/1.1", browser_headers(xmc=True) + ["Content-Length: 50"], short))
    b.add("js", "post-cfg-short", req("POST /config HTTP/1.1", browser_headers(xmc=True) + ["Content-Length: 50"], short),
          closed=True)
    # login flow as mc.js sends it
    b.add("js", "login-nonce", req("GET /?loginnonce HTTP/1.1", browser_headers(xmc=True)))
    b.add("js", "login-bad", req("GET /?login=%s.%s HTTP/1.1" % ("0" * 32, "f" * 64), browser_headers(xmc=True)))
    b.add("js", "logout", req("GET /?logout HTTP/1.1", browser_headers(xmc=True)))
    b.add("js", "session-info", req("GET /?page=info HTTP/1.1",
                                    browser_headers() + ["Cookie: theme=dark; mcs=" + SESSION_PLACEHOLDER]))
    b.add("js", "session-set", req("GET /setparam/?display=on HTTP/1.1",
                                   browser_headers(xmc=True) + ["Cookie: mcs=" + SESSION_PLACEHOLDER + "; x=1"]))


# ---------------------------------------------------------------- (c) Web API doc

CMD = re.compile(r"^\s*(/(?:setparam|getparam|callfunction)/\?[^\s]+)")


def doc_values(v: str) -> List[str]:
    if "|" in v and not v.startswith("%"):
        return [x for x in v.split("|") if x][:4]
    m = re.fullmatch(r"(-?\d+)\.\.(-?\d+)", v)
    if m:
        return [m.group(1), m.group(2)]
    if re.fullmatch(r"[A-Z]{2,}(?:\.[A-Za-z]+)?(?:[-;.:][A-Z.]+)*", v) or v in ("X.X", "X.XXX", "C"):
        return ["5"]
    return [v]


def from_doc(b: Builder) -> None:
    for ln in DOC.read_text(errors="replace").splitlines():
        m = CMD.match(ln)
        if not m:
            continue
        target = m.group(1)
        if "=" in target:
            name, val = target.split("=", 1)
            for v in doc_values(val):
                b.add("doc", name, req("GET %s=%s HTTP/1.1" % (name, v), curl_headers()))
        else:
            b.add("doc", target, req("GET %s HTTP/1.1" % target, curl_headers()))
            b.add("doc", "noxmc-" + target, req("GET %s HTTP/1.1" % target, curl_headers(xmc=False)))


# ---------------------------------------------------------------- (d) guard test matrices

STR = r'"((?:[^"\\]|\\.)*)"'
DEFINES = {"XMC": "X-MC: 1"}


def c_unescape(s: str) -> str:
    return bytes(s, "latin-1").decode("unicode_escape")


def guard_consts(txt: str) -> Dict[str, str]:
    d = dict(DEFINES)
    for m in re.finditer(r"static const char \*const (\w+) = " + STR + ";", txt):
        d[m.group(1)] = c_unescape(m.group(2))
    for m in re.finditer(r"#define (\w+) " + STR, txt):
        d.setdefault(m.group(1), c_unescape(m.group(2)))
    return d


def from_guard(b: Builder) -> None:
    txt = GUARD_TEST.read_text()
    consts = guard_consts(txt)
    for m in re.finditer(r"\breq\(\s*" + STR + r"\s*,\s*\{([^{}]*)\}\s*\)", txt):
        line = c_unescape(m.group(1))
        items = [x.strip() for x in re.split(r",(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)", m.group(2)) if x.strip()]
        hdrs: List[str] = []
        ok = True
        for it in items:
            lit = re.fullmatch(STR, it)
            if lit:
                hdrs.append(c_unescape(lit.group(1)))
            elif it in consts:
                hdrs.append(consts[it])
            else:
                ok = False  # computed at run time (h.c_str(), ...): not a literal request
                break
        if ok and re.match(r"^[A-Z]+ \S* ?HTTP/1\.\d$|^[A-Z]+ \S+", line):
            b.add("grd", line.split(" ")[1] if " " in line else line, req(line, hdrs))
    # the Sec-Fetch-Site matrix and the <img> CSRF matrix, as the test builds them
    sites = [None, "same-origin", "none", "NONE", "cross-site", "same-site", "Cross-Site", "", "some-future-value",
             "same-origin, cross-site"]
    for rl in re.findall(r'run_fs_matrix\(\s*' + STR, txt):
        line = c_unescape(rl)
        for s in sites:
            sf = ("Sec-Fetch-Site: " + s) if s is not None else "X-Other: 1"
            b.add("grd", "fs-" + line.split(" ")[1], req(line, ["Host: 192.168.68.71", sf, "X-MC: 1",
                                                                "Sec-Fetch-Mode: cors", "Sec-Fetch-Dest: empty"]))
    m = re.search(r"targets\[\] = \{(.*?)\};", txt, re.S)
    if m:
        for t in re.findall(STR, m.group(1)):
            t = c_unescape(t)
            for extra in ("X-Other: 1", "X-MC: 1"):
                b.add("grd", "img-" + t, req("GET %s HTTP/1.1" % t, [
                    "Host: 192.168.68.71", "Connection: keep-alive", "User-Agent: " + BROWSER_UA,
                    "Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8",
                    "Referer: http://attacker.example/", "Accept-Encoding: gzip, deflate", extra]))


# ---------------------------------------------------------------- (e) mutations

def from_mutations(b: Builder) -> None:
    H = curl_headers()
    # HTTP version variants of every handler family
    targets = ["/", "/?page=info", "/setparam/?onewire=on", "/getparam/?gateway", "/callfunction/?sendpos",
               "/?sendmessage&tocall=9999&message=hi%20there", "/pinstatus", "/rmstatus", "/mc.js"]
    for t in targets:
        for ver in ("HTTP/1.0", "HTTP/1.1", "HTTP/2.0", "HTTP/1.1x", ""):
            line = ("GET %s %s" % (t, ver)).rstrip()
            b.add("mut", "%s-%s" % (t, ver or "nover"), req(line, H))
            b.add("mut", "%s-%s-nohdr" % (t, ver or "nover"), req(line, []))
    # method variants
    for meth in ("POST", "HEAD", "OPTIONS", "PUT", "DELETE", "get", "GET/", ""):
        for t in ("/", "/?page=info", "/setparam/?x=1", "/rmsend", "/config"):
            b.add("mut", "meth-%s-%s" % (meth or "none", t), req("%s %s HTTP/1.1" % (meth, t), H + ["Content-Length: 0"]))
    # missing '=', empty names and values
    for t in ("/setparam/?webserver", "/setparam/?", "/setparam/", "/setparam/?=", "/setparam/?=x", "/setparam/?a=",
              "/setparam/?a==b", "/setparam/?a=b=c", "/getparam/?", "/getparam/", "/getparam/?=x",
              "/getparam/?gateway=1", "/callfunction/?", "/callfunction/", "/callfunction/?=x", "/callfunction/?a=b=c"):
        b.add("mut", "noeq-" + t, req("GET %s HTTP/1.1" % t, H))
    # Referer / Origin / query values that contain a route token (D4)
    ref = ["/?page=info", "/setparam/", "/callfunction/", "/rmsend", "/config.json", "/getparam/", "/?sendmessage"]
    for r in ref:
        b.add("mut", "referer-" + r, req("GET /?page=wx HTTP/1.1", H + ["Referer: http://%s%s" % (HOST, r)]))
        b.add("mut", "referer-root-" + r, req("GET / HTTP/1.1", H + ["Referer: http://%s%s" % (HOST, r)]))
        b.add("mut", "referer-unk-" + r, req("GET /nothing HTTP/1.1", H + ["Referer: http://%s%s" % (HOST, r)]))
        b.add("mut", "query-" + r, req("GET /nothing?x=%s HTTP/1.1" % r, H))
    for t in ("/?page=infoX", "/?page=info&page=wx", "/?page=", "/?page=setupX", "/?xpage=info", "/x/?page=info",
              "/pinstatusX", "/pinstatus?x", "/xpinstatus", "/mc.jsX", "/mc.js/../x", "/MC.JS", "/rmstatus/", "/rmsendx",
              "/screen.oledX", "/config.jsonX", "/POST /config", "/?page=mcp23017", "/?page=mailbox",
              "/austatus?x", "/austatusX", "/?loginnonceX", "/?login=", "/?login=abc", "/?logoutX",
              "/?nodepassword=pw", "/?nodepassword", "/?nodepasswordX"):
        b.add("mut", "route-" + t, req("GET %s HTTP/1.1" % t, H))
    # percent edge cases in names/values/messages
    pct = ["%", "%4", "%41", "%4G", "%%41", "%2541", "%00", "%0D%0A", "%0d%0a", "%22", "%7F", "%1F", "%C3", "%C3%A4",
           "%E2%82%AC", "%F0%9F%98%80", "%C0%AF", "+", "%2B", "a+b", "%20", "%2", "%G1", "%u0041", "x%"]
    for p in pct:
        b.add("mut", "pct-set-" + p, req("GET /setparam/?atxt=%s HTTP/1.1" % p, H))
        b.add("mut", "pct-name-" + p, req("GET /setparam/?%s=on HTTP/1.1" % p, H))
        b.add("mut", "pct-get-" + p, req("GET /getparam/?%s HTTP/1.1" % p, H))
        b.add("mut", "pct-call-" + p, req("GET /callfunction/?%s=%s HTTP/1.1" % (p, p), H))
        b.add("mut", "pct-msg-" + p, req("GET /?sendmessage&tocall=%s&message=%s HTTP/1.1" % (p, p), H))
    # JSON-hostile names and values
    for p in ('a"b', "a\\b", "a<b>", "ä"):
        b.add("mut", "json-" + p, req("GET /setparam/?%s=%s HTTP/1.1" % (p.encode("latin-1", "replace").decode("latin-1"), p.encode("latin-1", "replace").decode("latin-1")), H))
    # Content-Length variants on POST handlers
    body = b"dst=DK5EN-99&pw=x&cmd=mh&args=5"
    cl = ["Content-Length: %d" % len(body), "content-length: %d" % len(body), "CONTENT-LENGTH:%d" % len(body),
          "Content-Length: 0", "Content-Length: -5", "Content-Length: 12abc", "Content-Length: abc",
          "Content-Length: 99999999999", "Content-Length: 2147483648", "Content-Length: 4294967296",
          "Content-Length: 261", "Content-Length: 260", "Content-Length: 259", "Content-Length: 6144",
          "Content-Length: 6145", "Content-Length: 65536", "Content-Length:", "Content-Length : 5", "X-Content-Length: 5"]
    for c in cl:
        for path in ("/rmsend", "/config"):
            b.add("mut", "cl-%s-%s" % (path, c), req("POST %s HTTP/1.1" % path, H + [c], body))
    b.add("mut", "cl-dup-same", req("POST /rmsend HTTP/1.1", H + ["Content-Length: 32", "Content-Length: 32"], body))
    b.add("mut", "cl-dup-diff", req("POST /rmsend HTTP/1.1", H + ["Content-Length: 5", "Content-Length: 32"], body))
    b.add("mut", "cl-dup-diff2", req("POST /rmsend HTTP/1.1", H + ["Content-Length: 32", "Content-Length: 5"], body))
    b.add("mut", "cl-huge-nobody", req("POST /config HTTP/1.1", H + ["Content-Length: 1000000"]))
    b.add("mut", "cl-huge-nobody", req("POST /config HTTP/1.1", H + ["Content-Length: 1000000"]), closed=True)
    b.add("mut", "cl-late", req("POST /rmsend HTTP/1.1", ["Host: " + HOST] + ["X-Pad: " + "p" * 1000, "Content-Length: 32"], body))
    for n in (10, 32, 33):
        b.add("mut", "cl-short-body%d" % n, req("POST /rmsend HTTP/1.1", H + ["Content-Length: 32"], body[:n]))
        b.add("mut", "cl-short-body%d" % n, req("POST /rmsend HTTP/1.1", H + ["Content-Length: 32"], body[:n]), closed=True)
    for path in ("/rmpasswd", "/rmwebpwd", "/rmnodes", "/rmsend", "/rmstatus", "/rmheard", "/config"):
        for meth in ("GET", "POST"):
            b.add("mut", "rm-%s-%s" % (meth, path), req("%s %s HTTP/1.1" % (meth, path), H + ["Content-Length: 8"], b"pw=abcde"))
    # header line length edges: 1022 / 1023 / 1024 byte lines, and a head over the 1023-byte guard window
    for n in (1021, 1022, 1023, 1024, 1025):
        line = "X-Long: " + "a" * (n - len("X-Long: "))
        b.add("mut", "line-%d" % n, req("GET /?page=info HTTP/1.1", ["Host: " + HOST, line]))
    for n in (1021, 1022, 1023, 1024):
        t = "/setparam/?atxt=" + "b" * (n - len("GET /setparam/?atxt= HTTP/1.1"))
        b.add("mut", "reqline-%d" % n, req("GET %s HTTP/1.1" % t, H))
    b.add("mut", "head-over-window", req("GET /?page=info HTTP/1.1", ["Host: " + HOST] + ["X-P%d: %s" % (i, "z" * 60) for i in range(30)] + ["X-MC: 1"]))
    b.add("mut", "head-over-window-route-late", req("GET /nothing HTTP/1.1", ["Host: " + HOST] + ["X-P%d: %s" % (i, "z" * 60) for i in range(16)] + ["Referer: /?page=info"]))
    b.add("mut", "head-over-window-cookie", req("GET /?page=info HTTP/1.1", ["Host: " + HOST] + ["X-P%d: %s" % (i, "z" * 60) for i in range(18)] + ["Cookie: mcs=" + SESSION_PLACEHOLDER]))
    # cookie variants
    for ck in ("mcs=" + SESSION_PLACEHOLDER, "a=b; mcs=" + SESSION_PLACEHOLDER, "mcs=" + SESSION_PLACEHOLDER + "; a=b",
               "xmcs=" + SESSION_PLACEHOLDER, "mcs=short", "mcs=", "COOKIE: mcs=" + SESSION_PLACEHOLDER, "a=" + "q" * 300 + "; mcs=" + SESSION_PLACEHOLDER):
        b.add("mut", "cookie-" + ck[:24], req("GET /?page=info HTTP/1.1", H + ["Cookie: " + ck]))
    b.add("mut", "cookie-two", req("GET /?page=info HTTP/1.1", H + ["Cookie: mcs=0", "Cookie: mcs=" + SESSION_PLACEHOLDER]))
    b.add("mut", "cookie-upper", req("GET /?page=info HTTP/1.1", H + ["cOOkie: mcs=" + SESSION_PLACEHOLDER]))
    # line endings and raw bytes
    b.add("mut", "bare-lf", ("GET /?page=info HTTP/1.1\nHost: %s\n\n" % HOST).encode())
    b.add("mut", "bare-lf-set", ("GET /setparam/?display=on HTTP/1.1\nHost: %s\nX-MC: 1\n\n" % HOST).encode())
    b.add("mut", "bare-cr", ("GET /?page=info HTTP/1.1\rHost: %s\r\r" % HOST).encode())
    b.add("mut", "nul-in-line", b"GET /?page=info HTTP/1.1\r\nHost: " + HOST.encode() + b"\r\nX-A: a\x00b\r\n\r\n")
    b.add("mut", "nul-in-reqline", b"GET /setparam/?atxt=a\x00b HTTP/1.1\r\nHost: " + HOST.encode() + b"\r\nX-MC: 1\r\n\r\n")
    b.add("mut", "high-bytes", b"GET /setparam/?atxt=\xc3\xa4\xff HTTP/1.1\r\nHost: " + HOST.encode() + b"\r\nX-MC: 1\r\n\r\n")
    b.add("mut", "empty-input", b"")
    b.add("mut", "empty-input", b"", closed=True)
    b.add("mut", "only-crlf", b"\r\n")
    b.add("mut", "only-crlf", b"\r\n", closed=True)
    b.add("mut", "double-blank", b"\r\n\r\nGET /?page=info HTTP/1.1\r\n\r\n")
    b.add("mut", "two-requests", req("GET /?page=info HTTP/1.1", H) + req("GET /?page=wx HTTP/1.1", H))
    b.add("mut", "space-prefix", req(" GET /?page=info HTTP/1.1", H))
    b.add("mut", "double-space", req("GET  /?page=info  HTTP/1.1", H))
    b.add("mut", "tab-sep", req("GET\t/?page=info\tHTTP/1.1", H))
    b.add("mut", "http-in-path", req("GET /x/HTTP/1.1/?page=info HTTP/1.1", H))
    b.add("mut", "http-in-query", req("GET /setparam/?atxt=HTTP/1.1 HTTP/1.1", H))
    b.add("mut", "http-in-msg", req("GET /?sendmessage&tocall=9999&message=see%20HTTP/1.1%20here HTTP/1.1", H))
    b.add("mut", "http-in-msg-raw", req("GET /?sendmessage&tocall=9999&message=see HTTP/1.1 here HTTP/1.1", H))
    b.add("mut", "msg-amp", req("GET /?sendmessage&tocall=9999&message=a&message=b&tocall=X HTTP/1.1", H))
    b.add("mut", "msg-order", req("GET /?sendmessage&message=m&tocall=9999 HTTP/1.1", H))
    b.add("mut", "callfunction-no-http", req("GET /callfunction/?sendpos", H))
    b.add("mut", "setparam-no-http", req("GET /setparam/?display=on", H + ["X-MC: 1"]))
    b.add("mut", "getparam-no-http", req("GET /getparam/?gateway", H))
    b.add("mut", "setparam-http-first", req("GET HTTP/1.1 /setparam/?display=on", H))
    b.add("mut", "path-only", req("/setparam/?display=on", H))
    b.add("mut", "login-forms", req("GET /?login=%s HTTP/1.1" % ("a" * 32 + "." + "b" * 64), H))
    b.add("mut", "login-amp", req("GET /?login=%s&x=1 HTTP/1.1" % ("a" * 32 + "." + "b" * 64), H))
    b.add("mut", "login-short", req("GET /?login=a.b HTTP/1.1", H))
    # deterministic truncations of a real GUI request, at the first 24 offsets and a few later ones
    full = req("GET /setparam/?display=on HTTP/1.1", browser_headers(xmc=True))
    for cut in list(range(0, 24)) + [40, 60, 100, 200, len(full) - 4, len(full) - 3, len(full) - 2, len(full) - 1]:
        b.add("mut", "trunc-%03d" % cut, full[:cut], closed=(cut % 2 == 1))
    post = req("POST /rmsend HTTP/1.1", H + ["Content-Length: 32"], body)
    for cut in (len(post) - 32, len(post) - 31, len(post) - 16, len(post) - 1):
        b.add("mut", "trunc-post-%03d" % cut, post[:cut], closed=True)


# ---------------------------------------------------------------- driver

def build() -> Builder:
    b = Builder()
    from_capture(b)
    from_js(b)
    from_doc(b)
    from_guard(b)
    from_mutations(b)
    return b


def render(b: Builder) -> Dict[str, bytes]:
    files = {n + ".req": d for n, d in sorted(b.items.items())}
    files["INDEX.txt"] = ("".join(n + "\n" for n in sorted(b.items))).encode()
    return files


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when the checked-in corpus differs")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    ap.add_argument("--stats", action="store_true")
    a = ap.parse_args(argv)
    b = build()
    files = render(b)
    if a.stats or a.check:
        print("corpus: %d requests; by source: %s" % (len(b.items), ", ".join("%s=%d" % kv for kv in sorted(b.by_source.items()))))
    if a.check:
        have = {p.name: p.read_bytes() for p in a.out.glob("*") if p.is_file()}
        bad = sorted(set(files) ^ set(have)) + sorted(n for n in files if n in have and files[n] != have[n])
        if bad:
            print("corpus differs from the generator (%d files), first: %s" % (len(bad), ", ".join(bad[:5])))
            return 1
        print("corpus is current")
        return 0
    a.out.mkdir(parents=True, exist_ok=True)
    for p in a.out.glob("*"):
        if p.is_file() and p.name not in files:
            p.unlink()
    for n, d in files.items():
        (a.out / n).write_bytes(d)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
