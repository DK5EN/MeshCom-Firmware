"""Source lint: user-controlled and over-the-air text must be HTML-escaped at the web page sinks.

RM ext W1b (verdict finding 6, stored XSS) plus the advisor rework. Remote management can write
node_name and node_atxt over the air, and received LoRa frames fill the RX-log ring, the neighbour
matrix callsigns and the mailbox src/dst; the node's own web pages must never print them raw.
This is a plain text scanner, not a C++ parser: known sink patterns plus generic rules.

Rules, per src/web_functions/*.cpp:
  1. The body of _create_setup_textinput_element() must escape its value argument
     (htmlEscape(inputValue)) where it prints value="%s". Every call site then is covered.
  2. Per argument: in every page print call (X.print/printf/println incl. web_client., target->,
     snprintf/sprintf) EACH argument that contains a tracked token must itself pass through
     htmlEscape( / rmHtmlEscape( (text, attribute) or urlEncode( (href query value; its output is
     percent-encoded and attribute-safe). An escaped neighbour argument does not cover it. Serial / printdeb / DEBUG_MSG lines are skipped.
  3. String concatenation (page += tracked, "<td>" + tracked): every operand with a tracked token
     must be escaped.
  4. Aliasing: assigning a tracked value to a local (const char *n = meshcom_settings.node_name;)
     is flagged unless the line carries the marker "// lint: escaped-at-sink" (the alias is then
     escaped where it is printed, and the print is scanned by rule 2 because the alias name is
     tracked too, see TRACKED).
Reviewed exceptions go into ALLOWLIST with a reason.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WEB_DIR = ROOT / "src" / "web_functions"

# settings, over-the-air buffers and view fields that carry free text from users or other nodes
TRACKED = (
    r"node_name|node_atxt|node_ssid|ringbufferRAWLoraRX"  # settings + RX-log ring
    r"|acall|bcall"  # path-table aliases of neighbour-matrix callsigns
    r"|(?:v|r0|r|rv|av|bv)\.call"  # nbr view call fields as printed
    r"|e->(?:src|dst)"  # mailbox / message-store src + dst callsigns
)
TRACKED_RE = re.compile(r"(?<!\w)(" + TRACKED + r")\b")
SETTING_RE = re.compile(r"\b(node_name|node_atxt|node_ssid|ringbufferRAWLoraRX)\b")

ESCAPE = re.compile(r"\b(htmlEscape|rmHtmlEscape|urlEncode)\s*\(")
PRINT = re.compile(r"(?:\b\w+\s*(?:\.|->)\s*(?:print|printf|println)|\b(?:snprintf|sprintf))\s*\(")
NOT_PAGE = re.compile(r"\b(Serial\d?\s*\.|printdeb|DEBUG_MSG)")
HELPER_CALL = re.compile(r"^\s*_create_setup_textinput_element\s*\(")
ALIAS_RE = re.compile(r"^\s*(?:const\s+)?(?:char|String|auto)\b[\s*&]*\w+\s*=[^=]")
MARKER = "lint: escaped-at-sink"

# (file name, substring of the statement) -> reason. Keep empty unless a reviewed reason exists.
ALLOWLIST: dict = {}


def strip_comment(line):
    """Drop a trailing // comment, but not a '//' inside a string literal (https://aprs.fi/...)."""
    quote = None
    i = 0
    while i < len(line):
        c = line[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif line.startswith("//", i):
            return line[:i]
        i += 1
    return line


def statements(text):
    """Yield (first line number, statement text) for ;-terminated statements, comments stripped."""
    buf, start = [], None
    for no, line in enumerate(text.splitlines(), 1):
        code = strip_comment(line)
        if not code.strip():
            continue
        if start is None:
            start = no
        buf.append(code)
        bare = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)  # a ';' inside a string literal ends nothing
        if ";" in bare or bare.strip().endswith("{") or bare.strip() == "}":
            yield start, " ".join(buf)
            buf, start = [], None


def split_top(text, seps):
    """Split text at top-level separator characters (outside parens, brackets and string literals)."""
    parts, depth, cur, i, quote = [], 0, [], 0, None
    while i < len(text):
        c = text[i]
        if quote:
            cur.append(c)
            if c == "\\" and i + 1 < len(text):
                cur.append(text[i + 1])
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
            cur.append(c)
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            depth -= 1
            cur.append(c)
        elif c in seps and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    parts.append("".join(cur))
    return parts


def call_args(stmt, open_idx):
    """Top-level arguments of the call whose '(' sits at open_idx."""
    depth, quote, i = 0, None, open_idx
    while i < len(stmt):
        c = stmt[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return split_top(stmt[open_idx + 1 : i], ",")
        i += 1
    return split_top(stmt[open_idx + 1 :], ",")


def helper_body(text):
    m = re.search(r"void\s+_create_setup_textinput_element\s*\([^)]*\)\s*\{", text)
    if not m:
        return None
    depth, i = 1, m.end()
    while i < len(text) and depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end() : i]


def scan_text(name, text):
    problems = []
    # rule 1: the helper's own body escapes its value (not any htmlEscape elsewhere in the file)
    body = helper_body(text)
    if body is not None and not re.search(r"value=\\\"%s\\\".{0,200}htmlEscape\s*\(\s*inputValue\s*\)", body, re.S):
        problems.append(
            f"{name}: _create_setup_textinput_element() prints value=\"%s\" without htmlEscape(inputValue)"
        )
    lines = text.splitlines()
    for no, line in enumerate(lines, 1):  # rule 4: aliasing
        code = strip_comment(line)
        if MARKER in line or not ALIAS_RE.match(code) or NOT_PAGE.search(code):
            continue
        rhs = code.split("=", 1)[1]
        if SETTING_RE.search(rhs) and not ESCAPE.search(rhs):
            problems.append(f"{name}:{no}: tracked setting aliased to a local without '// {MARKER}': {code.strip()[:120]}")
    for no, stmt in statements(text):
        if not TRACKED_RE.search(stmt) or NOT_PAGE.search(stmt) or HELPER_CALL.match(stmt):
            continue
        if any(name == f and frag in stmt for (f, frag) in ALLOWLIST):
            continue
        bad = []
        for m in PRINT.finditer(stmt):  # rule 2: per argument
            for arg in call_args(stmt, m.end() - 1):
                if TRACKED_RE.search(arg) and not ESCAPE.search(arg):
                    bad.append(arg.strip())
        if "+" in stmt and not PRINT.search(stmt) and (
            "+=" in stmt or re.search(r"\"\s*\+|\+\s*\"", stmt)
        ):  # rule 3: String concatenation
            for operand in split_top(stmt.replace("+=", "+"), "+"):
                if TRACKED_RE.search(operand) and not ESCAPE.search(operand):
                    bad.append(operand.strip())
        for arg in bad:
            problems.append(f"{name}:{no}: tracked value printed without htmlEscape: {arg[:100]}  [{stmt.strip()[:100]}]")
    return problems


def scan_dir(directory):
    out = []
    for path in sorted(Path(directory).glob("*.cpp")):
        out += scan_text(path.name, path.read_text(errors="replace"))
    return out


def test_web_pages_escape_user_controlled_settings():
    problems = scan_dir(WEB_DIR)
    assert not problems, "unescaped page sinks:\n" + "\n".join(problems)


def test_lint_catches_the_known_bad_patterns():
    """The scanner itself must stay red on the pre-fix shapes."""
    bad_text_node = 'web_client.printf("<tr><td>APRS text</td><td>%s</td></tr>\\n", meshcom_settings.node_atxt);\n'
    assert scan_text("x.cpp", bad_text_node)
    bad_helper = (
        "void _create_setup_textinput_element(const char id[], String inputValue){\n"
        '    web_client.printf("<input value=\\"%s\\">", inputValue.c_str());\n'
        "}\n"
    )
    assert scan_text("x.cpp", bad_helper)
    good = (
        "void _create_setup_textinput_element(const char id[], String inputValue){\n"
        '    web_client.printf("<input value=\\"%s\\">", htmlEscape(inputValue).c_str());\n'
        "}\n"
        'web_client.printf("<td>%s</td>", htmlEscape(String(meshcom_settings.node_atxt)).c_str());\n'
        'Serial.printf("%s", meshcom_settings.node_name);\n'
    )
    assert scan_text("x.cpp", good) == []


def test_lint_catches_the_review_bypasses():
    """One string per bypass the reviewer found; each must stay red."""
    # (a) an escaped argument next to an unescaped one in the same statement
    assert scan_text("x.cpp", 'web_client.printf("%s %s", htmlEscape(String(a)).c_str(), meshcom_settings.node_name);\n')
    # (b) local alias of a tracked setting without the marker; with the marker it is accepted
    assert scan_text("x.cpp", "const char *n = meshcom_settings.node_name;\n")
    assert scan_text("x.cpp", "const char *n = meshcom_settings.node_name; // lint: escaped-at-sink\n") == []
    # (c) String concatenation into a page buffer
    assert scan_text("x.cpp", "page += meshcom_settings.node_atxt;\n")
    assert scan_text("x.cpp", 'page = "<td>" + String(meshcom_settings.node_name) + "</td>";\n')
    # (d) target->printf / target.print (webUIComponents style) is scanned
    assert scan_text("x.cpp", 'target->printf("<td>%s</td>", meshcom_settings.node_atxt);\n')
    # (e) an htmlEscape elsewhere in the file does not vouch for the helper
    bad_helper = (
        "void _create_setup_textinput_element(const char id[], String inputValue){\n"
        '    web_client.printf("<input value=\\"%s\\">", inputValue.c_str());\n'
        "}\n"
        "void other(){ htmlEscape(inputValue); }\n"
    )
    assert scan_text("x.cpp", bad_helper)
    # over-the-air buffers (RX log ring, nbr view callsigns, mailbox src/dst)
    assert scan_text("x.cpp", 'web_client.printf("<p>%s</p>", ringbufferRAWLoraRX[iRead]);\n')
    assert scan_text("x.cpp", 'web_client.printf("<a href=\\"?call=%s\\">%s</a>", urlEncode(v.call).c_str(), v.call);\n')
    assert scan_text("x.cpp", 'web_client.printf("<td>%s</td>", e->src);\n')
    assert scan_text("x.cpp", "web_client.print(r0.call);\n")
    # a "//" inside a string literal (URL) must not hide the arguments behind it
    assert scan_text("x.cpp", 'web_client.printf("<a href=\\"https://aprs.fi/?call=%s\\">%s</a>", v.call, v.call);\n')
    # ';' inside the format string (JS in onclick) must not cut the statement before its arguments
    assert scan_text("x.cpp", 'web_client.printf("<b onclick=\\"f(\'%s\');g();\\">",\n    e->src);\n')
    ok = 'web_client.printf("<td>%s</td>", htmlEscape(String(e->src)).c_str());\n'
    assert scan_text("x.cpp", ok) == []
