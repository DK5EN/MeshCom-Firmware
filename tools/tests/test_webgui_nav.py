"""Sidebar structure check for src/web_functions/web_functions.cpp.

A nav button whose icon is a long base64 data URI is printed in several web_client.print() calls and ends
with web_client.println(). Inserting another button between those calls lands inside the open <img src="...">
and silently breaks both buttons (the Remote button did exactly that once). This test fails on such a line.
"""
import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "src" / "web_functions" / "web_functions.cpp"


def nav_lines():
    lines = SRC.read_text(encoding="utf-8").splitlines()
    start = next(i for i, l in enumerate(lines) if 'id=\\"nav_layer\\"' in l or "id=\\\"nav_layer\\\"" in l)
    end = next(i for i in range(start, len(lines)) if "Logout" in lines[i] or "logout" in lines[i])
    return lines[start : end + 3]


def test_no_button_starts_inside_an_open_multi_print_button():
    prev_open = False  # the previous print() was a continuation (no println yet)
    for l in nav_lines():
        m = re.search(r"web_client\.(print|println)\(", l)
        if not m:
            continue
        if "<Button" in l or "<button" in l:
            assert not prev_open, "a nav button starts inside an unfinished print() sequence: " + l.strip()[:90]
        prev_open = m.group(1) == "print"
    assert not prev_open


def test_nav_order_pins_after_wx_remote_after_position():
    # operator decision 2026-11-07: Pins right after Weather, Remote right after Position (before MHeard)
    text = "\n".join(nav_lines())
    i = {p: text.index("loadPage('%s',this,true)" % p) for p in ("wx", "pins", "position", "remote", "mheard", "setup")}
    assert i["wx"] < i["pins"] < i["position"] < i["remote"] < i["mheard"] < i["setup"]
