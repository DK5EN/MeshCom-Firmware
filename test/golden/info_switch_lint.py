#!/usr/bin/env python3
"""Keep the web info page's switch overview in step with the settings page.

``sub_page_setup()`` (in ``src/web_functions/web_functions.cpp``) creates
every settings-page toggle through one helper::

    _create_setup_switch_element("<id>", "<Label>", "<description>", <expr>...);

``sub_page_info()`` is supposed to print a matching overview -- one line per
switch, ``Label: value`` -- so a node's current state can be read off the
info page without opening the settings page. Nothing enforces that the two
stay in sync: a switch can be added, renamed or relabelled on the settings
page and the info page keeps showing the old label, or drops the row
entirely, and the compiler has no opinion about either.

This lint extracts every ``(id, label)`` pair from ``sub_page_setup()`` (by
brace-matching its body, so it does not care about ``#if``/``#else`` guards
or argument count) and checks that ``sub_page_info()``'s body contains the
literal ``"<Label>: `` for each one -- the label immediately inside a quoted
printf format, followed by a colon and a space. A switch is exempt only when
its id is listed in ``ALLOWED_MISSING`` below, with a reason.

    python3 test/golden/info_switch_lint.py
    python3 test/golden/info_switch_lint.py --self-test

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WEB_FUNCTIONS = REPO_ROOT / "src" / "web_functions" / "web_functions.cpp"

# Switch ids deliberately left off the info page. Keep this empty unless a
# switch has a real reason not to show there -- the reason is not optional.
ALLOWED_MISSING: dict[str, str] = {}

SWITCH_CALL = re.compile(
    r'_create_setup_switch_element\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"'
)


def mask_comment_lines(body: str) -> str:
    """Blank out lines that are entirely a `//` comment.

    A commented-out call, like the "NOT USED small Display" line in
    ``sub_page_setup()``, must not be picked up as a live switch. This only
    masks whole-line comments (the line's stripped text starts with `//`) --
    a trailing `// create Switch-Element ...` comment after a real call is
    left alone, since the call itself still precedes it on the same line.
    """
    out_lines: list[str] = []
    for line in body.splitlines():
        if line.strip().startswith("//"):
            out_lines.append("")
        else:
            out_lines.append(line)
    return "\n".join(out_lines)


def extract_function_body(text: str, func_name: str) -> str:
    """Return the `{ ... }` body of `func_name`'s definition, by brace match.

    Skips a bare prototype (`void foo();`, no body) and finds the first
    occurrence whose closing paren is followed by `{`.
    """
    search_from = 0
    while True:
        idx = text.find(func_name, search_from)
        if idx < 0:
            raise SystemExit(f"info_switch_lint: {func_name!r} not found")
        paren = text.find("(", idx)
        close_paren = text.find(")", paren)
        if paren < 0 or close_paren < 0:
            search_from = idx + len(func_name)
            continue
        after = text[close_paren + 1 :]
        stripped = after.lstrip()
        if not stripped.startswith("{"):
            search_from = idx + len(func_name)
            continue
        brace = close_paren + 1 + (len(after) - len(stripped))
        depth = 0
        i = brace
        while i < len(text):
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return text[brace + 1 : i]
            i += 1
        raise SystemExit(f"info_switch_lint: unbalanced braces in {func_name!r}")


def extract_switches(body: str) -> list[tuple[str, str]]:
    """Return (id, label) for every live `_create_setup_switch_element` call."""
    masked = mask_comment_lines(body)
    return SWITCH_CALL.findall(masked)


def label_present(info_body: str, label: str) -> bool:
    """True if `"<label>: ` occurs literally in the info page's body."""
    return f'"{label}: ' in info_body


def run(text: str) -> int:
    setup_body = extract_function_body(text, "sub_page_setup")
    info_body = extract_function_body(text, "sub_page_info")
    switches = extract_switches(setup_body)

    missing: list[tuple[str, str]] = []
    allowed_hits = 0
    for switch_id, label in switches:
        if switch_id in ALLOWED_MISSING:
            allowed_hits += 1
            continue
        if not label_present(info_body, label):
            missing.append((switch_id, label))

    if missing:
        for switch_id, label in missing:
            print(
                f"info_switch_lint: sub_page_info() is missing \"{label}: \" "
                f"for switch \"{switch_id}\" (sub_page_setup label {label!r})"
            )
        print(
            f"\ninfo_switch_lint: {len(missing)} of {len(switches)} switch(es) "
            f"not shown on the info page. Add a matching \"{{Label}}: \" line to "
            f"sub_page_info(), or list the id in ALLOWED_MISSING with a reason."
        )
        return 1

    print(
        f"info_switch_lint: OK - {len(switches)} switch(es) checked, "
        f"{allowed_hits} allow-listed, 0 missing"
    )
    return 0


SELF_TEST_SETUP_ALL_PRESENT = """
void sub_page_setup()
{
    _create_setup_switch_element("display", "Display", "keep display active", !bDisplayOff);
    _create_setup_switch_element("mesh", "Mesh", "enable mesh", bMESH);
    //NOT USED _create_setup_switch_element("small", "small Display", "reduce content", bSMALLDISPLAY);
}
"""

SELF_TEST_INFO_ALL_PRESENT = """
void sub_page_info()
{
    web_client.printf("Display: %s<br>", (!bDisplayOff ? "on" : "off"));
    web_client.printf("Mesh: %s<br>", (bMESH ? "on" : "off"));
}
"""

SELF_TEST_INFO_MISSING_MESH = """
void sub_page_info()
{
    web_client.printf("Display: %s<br>", (!bDisplayOff ? "on" : "off"));
}
"""


def self_test() -> int:
    bad = 0

    # extract_switches: two live calls, the commented-out one is ignored.
    switches = extract_switches(SELF_TEST_SETUP_ALL_PRESENT)
    if switches != [("display", "Display"), ("mesh", "Mesh")]:
        print(f"self-test FAIL: extract_switches got {switches!r}")
        bad += 1

    # All switches shown on the info page -> pass.
    combined = SELF_TEST_SETUP_ALL_PRESENT + SELF_TEST_INFO_ALL_PRESENT
    rc = run(combined)
    if rc != 0:
        print("self-test FAIL: expected pass when every label is present")
        bad += 1

    # One switch (Mesh) missing from the info page -> fail, naming it.
    combined_missing = SELF_TEST_SETUP_ALL_PRESENT + SELF_TEST_INFO_MISSING_MESH
    import io
    from contextlib import redirect_stdout

    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = run(combined_missing)
    out = buf.getvalue()
    if rc != 1:
        print("self-test FAIL: expected failure when a label is missing")
        bad += 1
    if '"mesh"' not in out or "Mesh: " not in out:
        print(f"self-test FAIL: missing-label report did not name mesh/Mesh:\n{out}")
        bad += 1

    # Allow-listed id is accepted even though its label is absent.
    global ALLOWED_MISSING
    saved = ALLOWED_MISSING
    ALLOWED_MISSING = {"mesh": "self-test: deliberately exempted"}
    try:
        rc = run(combined_missing)
        if rc != 0:
            print("self-test FAIL: allow-listed id should have passed")
            bad += 1
    finally:
        ALLOWED_MISSING = saved

    # Brace matching finds the right body across nested braces (an #if block
    # inside the function must not confuse it).
    nested = """
    void sub_page_setup()
    {
        if (x)
        {
            _create_setup_switch_element("a", "A", "d", true);
        }
        _create_setup_switch_element("b", "B", "d", false);
    }
    """
    nested_switches = extract_switches(extract_function_body(nested, "sub_page_setup"))
    if nested_switches != [("a", "A"), ("b", "B")]:
        print(f"self-test FAIL: nested-brace extraction got {nested_switches!r}")
        bad += 1

    if bad:
        return 1
    print("info_switch_lint self-test OK (5 checks)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true", help="check the lint itself")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    text = WEB_FUNCTIONS.read_text(encoding="utf-8", errors="replace")
    return run(text)


if __name__ == "__main__":
    sys.exit(main())
