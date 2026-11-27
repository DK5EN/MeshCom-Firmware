#!/usr/bin/env python3
"""Regenerates web_request_live.cpp and url_decode_live.cpp from the WORKING TREE (FORMAL-02 web differential, live entry).

Usage (from the repo root):
  python3 test/test_web_diff/live/extract_live.py          regenerate both files
  python3 test/test_web_diff/live/extract_live.py --check  regenerate in memory, compare with the committed files,
                                                           exit 1 with the head of a unified diff on a difference

Same idea as frozen/extract.sh, but the source is the working tree and the functions are found BY NAME, not by line
number: a function starts at its definition line (plus the comment block directly above it) and ends at the first "}"
in column 0. A name that is not found exactly once fails the run. The plumbing header, the MID block and the rename
list are read from frozen/extract.sh itself, so the two units cannot drift apart; the only difference is the suffix
(_live, namespace web_live). The generated file also carries the verbatim source text of every extracted segment
(kLiveSrc); test_web_diff.cpp checks that each of them is still in the working tree (staleness check).
"""
import difflib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
FROZEN_SH = os.path.join(ROOT, "test/test_web_diff/frozen/extract.sh")
OUT_DIR = os.path.join(ROOT, "test/test_web_diff/live")
WF = "src/web_functions/web_functions.cpp"
RH = "src/web_functions/web_rm_handlers.cpp"
UD = "src/url_decode.cpp"
RAW = "LIVESRC"


def die(msg):
    sys.exit("extract_live: " + msg)


def read_lines(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", newline="") as f:
        return f.read().split("\n")  # keeps "\r" if any, joined back with "\n" below


def only(lines, rx, what):
    hits = [i for i, l in enumerate(lines) if re.search(rx, l)]
    if len(hits) != 1:
        die("%s: expected exactly one match for /%s/, found %d" % (what, rx, len(hits)))
    return hits[0]


def comment_start(lines, i):
    """first line of the comment block that ends directly above line i"""
    while i > 0 and re.match(r"^(/\*\*|/\*|//| \*)", lines[i - 1]):
        i -= 1
    return i


def end_brace(lines, i, what):
    for j in range(i, len(lines)):
        if lines[j] == "}":
            return j
    die("%s: no closing brace in column 0" % what)


def seg_func(rel, lines, rx, what):
    i = only(lines, rx, what)
    return (rel, what, comment_start(lines, i), end_brace(lines, i, what))


def seg_line(rel, lines, rx, what):
    i = only(lines, rx, what)
    return (rel, what, comment_start(lines, i), i)


def seg_range(rel, lines, rx_first, rx_last, what):
    a, b = only(lines, rx_first, what), only(lines, rx_last, what)
    if b < a:
        die("%s: end before start" % what)
    return (rel, what, a, b)


def parse_frozen():
    sh = open(FROZEN_SH, encoding="utf-8").read()
    names = re.findall(r"s/\(\^\|\[\^A-Za-z0-9_\]\)(\w+)\\\(/\\1\1_frozen\(/g", sh)
    pre = re.search(r"<<'PRE'\n(.*?)\nPRE\n", sh, re.S)
    mid = re.search(r"<<'MID'\n(.*?)\nMID\n", sh, re.S)
    if not names or not pre or not mid:
        die("cannot parse frozen/extract.sh (rename list, PRE or MID block)")
    # every rename line of the sed script ends in "_frozen(/g"; a line the name regex cannot read must not
    # silently drop a rename (the live copy would then call the production function)
    renames = len(re.findall(r"_frozen\(/g", sh))
    if renames != len(names):
        die("frozen/extract.sh has %d rename lines (_frozen(/g) but %d were parsed" % (renames, len(names)))
    return names, pre.group(1), mid.group(1)


def generate():
    """returns {file name: text} of the two generated files"""
    names, pre, mid = parse_frozen()
    names = names + ["urlDecode"]  # the live decoder copy defines it next to url_percent_decode

    def ren(text):
        out = []
        for l in text.split("\n"):
            for n in names:
                l = re.sub(r"(^|[^A-Za-z0-9_])%s\(" % n, r"\g<1>%s_live(" % n, l)
            out.append(l)
        return "\n".join(out)

    wf, rh, ud = read_lines(WF), read_lines(RH), read_lines(UD)
    src = {WF: wf, RH: rh, UD: ud}
    segs = {
        "pre": [
            seg_line(WF, wf, r"^static bool web_cors_ok = true;", "web_cors_ok"),
            seg_range(WF, wf, r"^String web_header;", r"^bool bweb_server_running = false;", "web_header + clock globals"),
        ],
    }
    a = seg_line(WF, wf, r"^static char \*web_cfg_buf = NULL;", "web_cfg_buf")
    b = seg_func(WF, wf, r"^static int web_read_body\(", "web_read_body")
    segs["pre"].append((WF, "config buffer + web_read_body", a[2], b[3]))
    segs["rm"] = [
        seg_range(RH, rh, r"^#define RM_BODY_TIMEOUT_MS ", r"^#define RM_BODY_TOTAL_MS ", "rm body deadlines"),
        seg_func(RH, rh, r"^const char \*rmReadBody\(", "rmReadBody"),
    ]
    segs["asset"] = [seg_func(WF, wf, r"^static bool deliver_web_asset\(", "deliver_web_asset")]
    # router of work_webpage: webRouteLegacy (the WRL_* ids and the decision rules live in web_route.h), the disagreement counter and webRouteDecide, one block
    r0 = only(wf, r"^static uint8_t __attribute__\(\(noinline\)\) webRouteLegacy\(", "webRoute block start")
    rd = only(wf, r"^static uint8_t __attribute__\(\(noinline\)\) webRouteDecide\(", "webRouteDecide")
    route_seg = (WF, "webRouteLegacy + webRouteDecide", r0, end_brace(wf, rd, "webRouteDecide"))
    segs["rest"] = [
        seg_func(WF, wf, r"^WebHeadAcc \*web_head_acc_get\(", "web_head_acc_get"),
        route_seg,
        seg_func(WF, wf, r"^void work_webpage\(", "work_webpage"),
        seg_func(WF, wf, r"^String decodeURLPercentCoding\(", "decodeURLPercentCoding"),
        seg_func(WF, wf, r"^static String webDecodeSpan\(", "webDecodeSpan"),
        seg_func(WF, wf, r"^static WebSpan webCutAtNul\(", "webCutAtNul"),
        seg_func(WF, wf, r"^static int webFindLit\(", "webFindLit"),
        seg_func(WF, wf, r"^static WebSpan webParamText\(", "webParamText"),
        seg_func(WF, wf, r"^void send_http_header\(", "send_http_header"),
        seg_func(WF, wf, r"^void send_message\(", "send_message"),
        seg_func(WF, wf, r"^void call_function\(", "call_function"),
        seg_func(WF, wf, r"^static uint8_t webSetparamValueVerdict\(", "webSetparamValueVerdict"),
        seg_func(WF, wf, r"^void setparam\(", "setparam"),
        seg_func(WF, wf, r"^void getparam\(", "getparam"),
    ]
    # a segment must not overlap another one of the same file
    allsegs = [s for k in segs for s in segs[k]]
    for x in allsegs:
        for y in allsegs:
            if x is not y and x[0] == y[0] and not (x[3] < y[2] or y[3] < x[2]):
                die("segments overlap: %s / %s" % (x[1], y[1]))

    table = []  # (file, text) verbatim, for the staleness check

    def seg(s):
        rel, what, a, b = s
        text = "\n".join(src[rel][a : b + 1])
        table.append((rel, what, text))
        return "\n// ==== %s (%s) lines %d-%d (working tree) ====\n%s\n" % (what, os.path.basename(rel), a + 1, b + 1, ren(text))

    body = []
    body.append("// LIVE copy of the request path of the production web server (FORMAL-02 web differential, live entry).\n")
    body.append("// GENERATED by test/test_web_diff/live/extract_live.py from the working tree (src/web_functions/*.cpp, src/url_decode.cpp).\n")
    body.append("// Do not edit: functions are cut out by name, the only change is the entry-point rename (*_live) and the namespace.\n")
    body.append("// Regenerate after every edit of an extracted function:  python3 test/test_web_diff/live/extract_live.py\n")
    body.append("// Everything outside the marked ranges is this file's own plumbing, read from frozen/extract.sh.\n")
    live_pre = pre.replace("web_frozen", "web_live").replace("_frozen", "_live")
    # web_functions.cpp includes web_request.h itself (the head accumulator); the plumbing copies the
    # includes of the frozen unit, so the live unit adds it next to web_functions.h (outside the namespace)
    if live_pre.count("#include <web_functions.h>") != 1:
        die("plumbing of frozen/extract.sh: expected exactly one #include <web_functions.h>")
    live_pre = live_pre.replace("#include <web_functions.h>", "#include <web_functions.h>\n#include <web_request.h>\n#include <web_query.h>\n#include <command_dash.h>")
    # step 5a: setparam / getparam take the parsed request; the plumbing's forward declarations (read from the frozen unit,
    # which still has the String forms) are replaced here
    for old, new in (("void setparam_live(String web_header);", "void setparam_live(const WebReq &wr, const String &full_header);"),
                     ("void getparam_live(String web_header);", "void getparam_live(const WebReq &wr);"),
                     ("void call_function_live(String web_header);", "void call_function_live(const WebReq &wr, const String &full_header);"),
                     ("void send_message_live(String web_header);", "void send_message_live(const WebReq &wr);")):
        if live_pre.count(old) != 1:
            die("plumbing of frozen/extract.sh: expected exactly one '%s'" % old)
        live_pre = live_pre.replace(old, new)
    # web_route.h reads no build macro (the feature mask is computed in webRouteDecide); it is included after the
    # ESP32 define of the plumbing, as web_functions.cpp includes it with the board flags already set
    if live_pre.count("#define ESP32 1\n") != 1:
        die("plumbing of frozen/extract.sh: expected exactly one '#define ESP32 1'")
    live_pre = live_pre.replace("#define ESP32 1\n", "#define ESP32 1\n#include <web_route.h>\n")
    body.append(ren(live_pre) + "\n")
    body.append("size_t urlDecode_live(char *buf, size_t len, uint8_t policy); // own plumbing: the live decoder (url_decode_live.cpp)\n")
    for s in segs["pre"]:
        body.append(seg(s))
    body.append(seg(segs["rm"][0]))
    body.append(seg(segs["rm"][1]))
    body.append("\n" + mid.replace("_frozen", "_live") + "\n")
    for s in segs["asset"] + segs["rest"]:
        body.append(seg(s))
    # the real decoder source, copied by url_decode_live.cpp: part of the staleness table as well
    table.append((UD, "url_decode.cpp (whole file)", "\n".join(ud)))
    body.append("\n} // namespace web_live\n")

    # staleness table: raw strings; the delimiter must not occur in the sources
    body.append("\nnamespace web_live\n{\n")
    body.append("struct LiveSrc\n{\n    const char *file;\n    const char *what;\n    const char *text;\n};\n")
    body.append("extern const LiveSrc kLiveSrc[];\nextern const unsigned long kLiveSrcCount;\n")
    body.append("const LiveSrc kLiveSrc[] = {\n")
    for rel, what, text in table:
        if (")" + RAW + '"') in text:
            die("raw string delimiter occurs in " + what)
        body.append('    {"%s", "%s", R"%s(%s)%s"},\n' % (rel, what, RAW, text, RAW))
    body.append("};\nconst unsigned long kLiveSrcCount = sizeof(kLiveSrc) / sizeof(kLiveSrc[0]);\n} // namespace web_live\n")
    outputs = {"web_request_live.cpp": "".join(body)}

    # url decoder: whole file; the leading #include lines go in front of the namespace
    k = 0
    while ud[k].startswith("#include"):
        k += 1
    head = "\n".join(ud[:k])
    out = [
        "// LIVE copy of src/url_decode.cpp (the percent decoder the live request path calls), FORMAL-02 web diff.\n",
        "// GENERATED by extract_live.py; whole file from the working tree, the only change is the rename\n",
        "// url_percent_decode -> url_percent_decode_live, urlDecode -> urlDecode_live and the namespace web_live.\n",
        "#include <stddef.h>\n#include <stdint.h>\n", head + "\n", "namespace web_live\n{\nsize_t urlDecode_live(char *buf, size_t len, uint8_t policy); // own plumbing: the source defines it after its first use\n",
        ren("\n".join(ud[k:])), "\n} // namespace web_live\n",
    ]
    outputs["url_decode_live.cpp"] = "".join(out)
    return outputs, len(allsegs), len(table)


def main(argv):
    check = "--check" in argv
    if [a for a in argv if a != "--check"]:
        die("usage: extract_live.py [--check]")
    outputs, nseg, ntab = generate()
    if check:
        bad = False
        for name, text in outputs.items():
            path = os.path.join(OUT_DIR, name)
            try:
                with open(path, encoding="utf-8", newline="") as f:
                    have = f.read()
            except OSError as e:
                print("extract_live --check: cannot read %s: %s" % (name, e))
                bad = True
                continue
            if have != text:
                bad = True
                diff = list(difflib.unified_diff(have.split("\n"), text.split("\n"), "committed/" + name, "generated/" + name, lineterm="", n=2))
                print("extract_live --check: %s differs from the working tree (%d diff lines); head:" % (name, len(diff)))
                print("\n".join(l[:200] for l in diff[:40]))
        if bad:
            print("extract_live --check: regenerate with  python3 test/test_web_diff/live/extract_live.py")
            sys.exit(1)
        print("extract_live --check: ok (%d segments, %d table entries)" % (nseg, ntab))
        return
    for name, text in outputs.items():
        with open(os.path.join(OUT_DIR, name), "w", encoding="utf-8", newline="") as f:
            f.write(text)
    print("extract_live: %d segments, %d table entries" % (nseg, ntab))


main(sys.argv[1:])
