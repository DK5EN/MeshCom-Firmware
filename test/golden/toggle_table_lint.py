#!/usr/bin/env python3
"""Structural checks for `COMMAND_TOGGLES[]`, the D2-06 toggle table.

``src/command_toggles.h`` explains why 70 of the ladder's 107 `--<name> on` /
`--<name> off` rungs were hoisted into a data table (``toggleApply()``): each
one did nothing but a fixed subset of "set a flag, mask a persisted register,
run a post hook, save, echo, notify BLE" -- 953 source lines collapsing into
one struct array. That collapse only stays safe if the table itself keeps a
handful of invariants the compiler cannot check, because ``ToggleRow`` is
just a bag of a pointer, two ``uint32_t``s and a couple of bitfields -- C++
lets every one of the mistakes below compile cleanly and misbehave at
runtime, months later, on whichever board happens to exercise that row.

WHAT EACH CHECK IS FOR
-----------------------
1. **Name shape.** ``toggleApply()`` calls ``commandMatches(msg, row.name+2)``
   -- the leading "--" is a fixed offset, not parsed, so a name missing it
   corrupts every row after it by mis-indexing. The trailing "on"/"off" is
   the table's whole reason to exist: this is an on/off toggle table, not a
   general command table, and a row that is not one of those two words is a
   sign an argument-form rung was hoisted somewhere it cannot work (argument
   forms are prefix matches with no "on"/"off" -- ``toggleApply()`` has no
   code path for them at all).

2. **No duplicate names.** ``toggleApply()`` returns on the first match
   (D2-10's exact-token loop over the array); a later row with the same name
   is silently unreachable dead code, exactly the ``setowndns`` bug that
   ``command_ladder_lint.py`` found in the hand-written ladder this table was
   carved out of.

3. **No cross-row shadowing.** D2-06's own docstring claims hoisting the
   table ahead of the remaining hand-written ladder was checked "mechanically
   in both directions". This is the mechanical check, generalised to catch
   the SAME failure mode happening again entirely inside the table: an
   argument-form row (name ending in a space) is a prefix match under
   ``commandMatches()``, so if one were ever added here it could swallow
   another row's exact-token name the same way the old prefix-matching
   ladder swallowed ``--softser app0`` under ``--softser app``. The table
   holds no argument-form rows today, so this currently proves a negative;
   it is here so the day one is added, the lint -- not a bench run -- is
   what notices.

4. **No table row also hand-written.** ``COMMAND_TOGGLES`` is consulted
   BEFORE the remaining hand-written ladder (``commandAction()``). If a name
   exists in both places, the table's exact-token match always wins and the
   hand-written rung is dead code that nobody will ever see execute --
   worse, if the two implementations were ever edited independently, the
   hand-written one is a landmine that looks live in a diff but silently
   never runs.

5. **Mask/register pairing.** ``toggleApply()`` applies the mask
   unconditionally whenever ``row.sset`` is non-null:
   ``*sset = (*sset & and_mask) | or_mask``. A non-null ``sset`` with a
   trivial mask (``0xFFFFFFFF``, ``0x00000000``) touches a persisted
   register and changes nothing -- the pointer was very likely meant to
   carry a real bit and the bit got left off in transcription. A null
   ``sset`` with a non-trivial mask is worse: the mask is simply discarded
   (there is no register to apply it to), so whoever wrote that row believed
   a bit was being persisted and it never was.

6. **TG_FLAG_TRUE needs a flag.** ``toggleApply()`` only ever writes
   ``*row.flag`` when ``row.flag`` is non-null; ``TG_FLAG_TRUE`` on a
   ``nullptr`` flag is opt-bits noise that documents an intent the row
   cannot carry out.

7. **On/off pair complementarity (WARNING, not a hard failure).** For a
   base name with both an "on" and an "off" row sharing one register, the
   check is: the bit ``on`` sets is one ``off`` clears
   (``off.and_mask & on.or_mask == 0``) and ``off`` sets nothing of its own
   (``off.or_mask == 0``). This is deliberately a warning, not an exit-1
   failure, and deliberately this loose, not "``off.and_mask`` is exactly
   ``~on.or_mask``" -- because of two things ``command_toggles.h`` documents
   on purpose:

   - ``mesh on`` / ``mesh off``: the stored bit means "mesh OFF", so ``on``
     CLEARS it (``or_mask == 0``) and ``off`` SETS it
     (``off.or_mask == 0x0020``) -- the one row in the table where "on" and
     "off" are semantically inverted from their bit's name. This DOES trip
     the check above (``off.or_mask != 0``) and is the one warning the real
     tree produces today.
   - ``button off`` (``0x7FEF``), ``setcont off`` (``0x3FFF``),
     ``shortpath off`` (``0x7BFF``), ``setlog off`` (``0x7FFB``): each ANDs
     off a SECOND bit (bit 15 in every case) as a side effect that has
     nothing to do with the row's own bit -- see ``command_toggles.h``'s
     "WHY THE MASK IS A PAIR" section. Because the check above only asks
     whether ``off`` clears AT LEAST the bit ``on`` set, not EXACTLY that
     bit and no other, these four pass cleanly and warn about nothing.
     They are named here so a future reader tempted to tighten the check to
     "exactly that bit" (which would newly flag all four as violations)
     knows that extra bit is intentional and the loose check is correct as
     it stands -- do not tighten it.

8. **Phone answer.** Over BLE a row answers the phone in exactly one of two
   ways: a settings JSON (a ``TG_DIRTY_*`` class other than
   ``TG_DIRTY_NONE`` sets ``bNodeSetting``/..., and ``commandAction()``'s
   tail sends ``SN``/``SE``/...) or a text echo (``TG_BLE_ECHO``,
   ``addBLECommandBack()``), which the app shows as a chat line. Both on one
   row means the phone gets the JSON plus a stray chat line -- a hard
   failure. Rows in ``SETTINGS_ANSWER`` are pinned to their JSON class
   because upstream changed their rung from the text echo to the JSON and a
   hoisted table row does not follow such an upstream change by itself:
   ``--via on``/``--via off`` answer with ``sendNodeSetting()`` (SN + SN1)
   since upstream d93c05a0/31ef8648, and the 2026-09-25 upstream merge
   silently dropped that fix here until this check pinned it.

    python3 test/golden/toggle_table_lint.py
    python3 test/golden/toggle_table_lint.py --self-test

Exit 0 when every hard check (1-6, 8) passes, 1 when any fails. Check 7's
findings are printed as warnings and never affect the exit code.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
COMMANDS = REPO / "src" / "command_functions.cpp"

TABLE_START = "static const ToggleRow COMMAND_TOGGLES[]"

NAME_RE = re.compile(r"^[a-z0-9_]+(?: [a-z0-9_]+)* (on|off)$")
ROW_RE = re.compile(r'^\s*\{\s*"([^"]*)"\s*,\s*(.*)\}\s*,?\s*$')
GUARD_OPEN_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b(.*)$")
GUARD_ELIF_RE = re.compile(r"^\s*#\s*(elif|else)\b(.*)$")
GUARD_END_RE = re.compile(r"^\s*#\s*endif\b(.*)$")

# opt bits, mirrored from command_toggles.h (kept as raw text, not imported --
# this lint runs on the host with no compiler, so it greps the same integers
# the header #defines rather than parsing the C preprocessor).
TG_FLAG_TRUE = re.compile(r"\bTG_FLAG_TRUE\b")

TRIVIAL_AND = 0xFFFFFFFF
TRIVIAL_OR = 0x00000000

# Check 8: rows whose answer to the phone is a settings JSON by upstream's
# rung, pinned to that dirty class (see the module docstring).
TG_BLE_ECHO = re.compile(r"\bTG_BLE_ECHO\b")
SETTINGS_ANSWER = {
    "via on": "TG_DIRTY_NODE",
    "via off": "TG_DIRTY_NODE",
}


class Row:
    __slots__ = (
        "name", "flag", "sset", "and_mask", "or_mask", "post", "dirty",
        "opt", "guard", "lineno", "raw",
    )

    def __init__(self, name, flag, sset, and_mask, or_mask, post, dirty,
                 opt, guard, lineno, raw):
        self.name = name
        self.flag = flag
        self.sset = sset
        self.and_mask = and_mask
        self.or_mask = or_mask
        self.post = post
        self.dirty = dirty
        self.opt = opt
        self.guard = guard
        self.lineno = lineno
        self.raw = raw

    @property
    def bare(self) -> str:
        """The name with its leading '--' stripped, e.g. 'debug on'."""
        return self.name[2:] if self.name.startswith("--") else self.name

    @property
    def base(self) -> str:
        """The bare name with a trailing ' on' / ' off' stripped."""
        b = self.bare
        for suffix in (" on", " off"):
            if b.endswith(suffix):
                return b[: -len(suffix)]
        return b

    def where(self) -> str:
        loc = "line %d" % self.lineno
        return "%s (guard: %s)" % (loc, self.guard) if self.guard else loc


def _parse_int(text: str) -> int:
    return int(text.strip(), 0)


def extract_table_text(text: str) -> str:
    """The COMMAND_TOGGLES[] initializer, braces and #if guards included."""
    start = text.index(TABLE_START)
    end = text.index("\n};", start) + len("\n};")
    return text[start:end]


def parse_rows(table_text: str, start_lineno: int = 1) -> list[Row]:
    """Every row in the table, in source order, guard included.

    Rows are always single physical lines in this table (verified against
    the real file), so this is a line-by-line scan rather than a brace-aware
    parser. A `#if`/`#ifdef`/`#ifndef` ... `#endif` stack is tracked purely
    so each row can report which guard it lives under -- guards are NOT used
    to filter rows out: a row disabled on this board must still obey every
    invariant below, because it is live on some other board.
    """
    rows: list[Row] = []
    guard_stack: list[str] = []

    for i, line in enumerate(table_text.splitlines(), start=start_lineno):
        m = GUARD_OPEN_RE.match(line)
        if m:
            guard_stack.append(line.strip())
            continue
        m = GUARD_ELIF_RE.match(line)
        if m and guard_stack:
            guard_stack[-1] = line.strip()
            continue
        m = GUARD_END_RE.match(line)
        if m and guard_stack:
            guard_stack.pop()
            continue

        m = ROW_RE.match(line)
        if not m:
            continue

        name = m.group(1)
        fields = [p.strip() for p in m.group(2).split(",")]
        if len(fields) != 7:
            # Not a data row (e.g. it matched inside a comment) -- skip
            # rather than crash; the row-count check at the bottom of
            # main() will notice if this silently drops real rows.
            continue

        flag, sset, and_mask, or_mask, post, dirty, opt = fields
        rows.append(Row(
            name=name,
            flag=flag,
            sset=sset,
            and_mask=_parse_int(and_mask),
            or_mask=_parse_int(or_mask),
            post=post,
            dirty=dirty,
            opt=opt,
            guard=" && ".join(guard_stack),
            lineno=i,
            raw=line.strip(),
        ))

    return rows


def commands_matches(msg: str, command: str) -> bool:
    """Python mirror of commandMatches() in src/command_match.h.

    `command` ending in a space is the argument form (prefix match);
    otherwise the match must end exactly at end-of-string, a space, CR or
    LF. Both sides here are already "--"-stripped table names, so CR/LF
    never occur in practice -- kept for a literal, not approximate, mirror.
    """
    clen = len(command)
    if len(msg) < clen:
        return False
    if msg[:clen].lower() != command.lower():
        return False
    if clen > 0 and command[-1] == " ":
        return True
    tail = msg[clen:clen + 1]
    return tail in ("", " ", "\r", "\n")


# ---------------------------------------------------------------------------
# checks


def check_name_shape(rows: list[Row]) -> list[str]:
    out = []
    for row in rows:
        if not row.name.startswith("--"):
            out.append('%s: name %r does not start with "--"' % (row.where(), row.name))
            continue
        if not NAME_RE.match(row.bare):
            out.append(
                '%s: name %r (bare %r) does not match "<lowercase tokens> (on|off)"'
                % (row.where(), row.name, row.bare)
            )
    return out


def check_duplicates(rows: list[Row]) -> list[str]:
    seen: dict[str, Row] = {}
    out = []
    for row in rows:
        prior = seen.get(row.name)
        if prior is not None:
            out.append(
                "%s: duplicate name %r, first seen at %s"
                % (row.where(), row.name, prior.where())
            )
        else:
            seen[row.name] = row
    return out


def check_shadowing(rows: list[Row]) -> list[str]:
    out = []
    for i, a in enumerate(rows):
        for j, b in enumerate(rows):
            if i == j:
                continue
            if commands_matches(a.bare, b.bare):
                out.append(
                    "%s: name %r is shadowed by %r at %s under the D2-10 "
                    "exact-token rule"
                    % (a.where(), a.name, b.name, b.where())
                )
    return out


def check_hand_written_overlap(rows: list[Row], commands_text: str) -> list[str]:
    out = []
    for row in rows:
        pattern = (
            r'commandCheck\(\s*msg_text\s*\+\s*2\s*,\s*\(char\s*\*\)\s*"'
            + re.escape(row.bare)
            + r'"\s*\)'
        )
        if re.search(pattern, commands_text):
            out.append(
                '%s: %r also has a hand-written commandCheck(msg_text+2, '
                '(char*)"%s") rung -- the table row always wins, the '
                "hand-written rung is dead" % (row.where(), row.name, row.bare)
            )
    return out


def check_mask_register_pairing(rows: list[Row]) -> list[str]:
    out = []
    for row in rows:
        trivial = row.and_mask == TRIVIAL_AND and row.or_mask == TRIVIAL_OR
        if row.sset == "nullptr":
            if not trivial:
                out.append(
                    "%s: sset is nullptr but mask is non-trivial "
                    "(and=0x%08X or=0x%08X) -- discarded, no register to apply it to"
                    % (row.where(), row.and_mask, row.or_mask)
                )
        else:
            if trivial:
                out.append(
                    "%s: sset is %s but mask is the trivial no-op "
                    "(and=0xFFFFFFFF or=0x00000000) -- register with no mask"
                    % (row.where(), row.sset)
                )
    return out


def check_flag_true_needs_flag(rows: list[Row]) -> list[str]:
    out = []
    for row in rows:
        if TG_FLAG_TRUE.search(row.opt) and row.flag == "nullptr":
            out.append(
                "%s: TG_FLAG_TRUE set but flag is nullptr" % row.where()
            )
    return out


def check_phone_answer(rows: list[Row], require_pinned: bool) -> list[str]:
    out = []
    for row in rows:
        if TG_BLE_ECHO.search(row.opt) and row.dirty != "TG_DIRTY_NONE":
            out.append(
                "%s: TG_BLE_ECHO together with %s -- the phone gets the "
                "settings JSON and a stray text echo" % (row.where(), row.dirty)
            )
    by_name = {row.bare: row for row in rows}
    for name, dirty in sorted(SETTINGS_ANSWER.items()):
        row = by_name.get(name)
        if row is None:
            # Only the real table must carry the pinned rows; synthetic
            # self-test tables usually hold other rows.
            if require_pinned:
                out.append("'--%s': pinned phone answer, but no such row" % name)
            continue
        if row.dirty != dirty or TG_BLE_ECHO.search(row.opt):
            out.append(
                "%s: must answer the phone with a settings JSON (%s, no "
                "TG_BLE_ECHO), has %s / %s" % (row.where(), dirty, row.dirty, row.opt)
            )
    return out


def check_pair_complementarity(rows: list[Row]) -> list[str]:
    """WARNING-only: see check 7 in the module docstring for why."""
    by_base: dict[str, dict[str, Row]] = {}
    for row in rows:
        b = row.bare
        if b.endswith(" on"):
            by_base.setdefault(row.base, {})["on"] = row
        elif b.endswith(" off"):
            by_base.setdefault(row.base, {})["off"] = row

    out = []
    for base, pair in sorted(by_base.items()):
        on, off = pair.get("on"), pair.get("off")
        if on is None or off is None:
            continue
        if on.sset == "nullptr" or off.sset == "nullptr":
            continue
        if on.sset != off.sset:
            continue
        ok = (off.and_mask & on.or_mask) == 0 and off.or_mask == 0
        if not ok:
            out.append(
                "%s / %s: '%s' pair is not bit-complementary "
                "(on.or_mask=0x%08X off.and_mask=0x%08X off.or_mask=0x%08X)"
                % (on.where(), off.where(), base, on.or_mask, off.and_mask, off.or_mask)
            )
    return out


def run_checks(table_text: str, commands_text: str, start_lineno: int = 1,
               require_pinned: bool = False):
    """Returns (rows, hard_failures, warnings)."""
    rows = parse_rows(table_text, start_lineno=start_lineno)
    hard = []
    hard += check_name_shape(rows)
    hard += check_duplicates(rows)
    hard += check_shadowing(rows)
    hard += check_hand_written_overlap(rows, commands_text)
    hard += check_mask_register_pairing(rows)
    hard += check_flag_true_needs_flag(rows)
    hard += check_phone_answer(rows, require_pinned)
    warnings = check_pair_complementarity(rows)
    return rows, hard, warnings


# ---------------------------------------------------------------------------
# self-test

GOOD_ROW_TMPL = (
    '    {{ "--{name}", {flag}, {sset}, {and_mask}, {or_mask}, nullptr, '
    "TG_DIRTY_NONE, {opt} }},\n"
)


def make_table(rows_text: str) -> str:
    return "static const ToggleRow COMMAND_TOGGLES[] =\n{\n" + rows_text + "};\n"


def self_test() -> int:
    ok = True

    def expect(name: str, cond: bool) -> None:
        nonlocal ok
        print(("  ok  " if cond else "SELF-TEST FAIL: ") + name)
        ok = ok and cond

    def messages(table_text: str, commands_text: str = ""):
        _, hard, warn = run_checks(table_text, commands_text)
        return hard, warn

    clean_pair = (
        GOOD_ROW_TMPL.format(
            name="debug on", flag="&bDEBUG", sset="&meshcom_settings.node_sset",
            and_mask="0xFFFFFFFF", or_mask="0x0008", opt="TG_SAVE | TG_FLAG_TRUE",
        )
        + GOOD_ROW_TMPL.format(
            name="debug off", flag="&bDEBUG", sset="&meshcom_settings.node_sset",
            and_mask="0xFFFFFFF7", or_mask="0x00000000", opt="TG_SAVE",
        )
    )

    # -- a clean table passes every check ------------------------------
    hard, warn = messages(make_table(clean_pair))
    expect("a clean on/off pair has no hard failures", hard == [])
    expect("a clean on/off pair has no complementarity warning", warn == [])

    # -- check 1: name shape --------------------------------------------
    bad_name = GOOD_ROW_TMPL.format(
        name="Debug on", flag="&bDEBUG", sset="nullptr",
        and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
    )
    hard, _ = messages(make_table(bad_name))
    expect("uppercase name is reported", any("Debug on" in h for h in hard))

    no_on_off = GOOD_ROW_TMPL.format(
        name="debug toggle", flag="&bDEBUG", sset="nullptr",
        and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
    )
    hard, _ = messages(make_table(no_on_off))
    expect("a name not ending in on/off is reported", any("debug toggle" in h for h in hard))

    # -- check 2: duplicates ---------------------------------------------
    dup = GOOD_ROW_TMPL.format(
        name="debug on", flag="&bDEBUG", sset="nullptr",
        and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
    ) * 2
    hard, _ = messages(make_table(dup))
    expect("a duplicate row name is reported", any("duplicate name" in h for h in hard))

    # -- check 3: shadowing (argument-form row swallowing another) ------
    shadow = (
        GOOD_ROW_TMPL.format(
            name="setlog ", flag="nullptr", sset="nullptr",
            and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="0",
        )
        + GOOD_ROW_TMPL.format(
            name="setlog on", flag="&bDisplayLog", sset="nullptr",
            and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
        )
    )
    hard, _ = messages(make_table(shadow))
    expect("an argument-form row shadowing a later on/off row is reported",
           any("shadowed" in h for h in hard))
    # ("setlog " itself does not satisfy NAME_RE, so this table also trips
    # check 1 -- that is fine, both are real defects in this synthetic row.)

    # -- check 4: hand-written overlap ------------------------------------
    overlap_table = make_table(GOOD_ROW_TMPL.format(
        name="gateway on", flag="&bGATEWAY", sset="nullptr",
        and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
    ))
    overlap_commands = 'if(commandCheck(msg_text+2, (char*)"gateway on") == 0)\n'
    hard, _ = messages(overlap_table, overlap_commands)
    expect("a name also hand-written as commandCheck(...) is reported",
           any("hand-written" in h for h in hard))

    no_overlap_commands = 'if(commandCheck(msg_text+2, (char*)"gateway off") == 0)\n'
    hard, _ = messages(overlap_table, no_overlap_commands)
    expect("a hand-written rung for a DIFFERENT name does not false-positive",
           not any("hand-written" in h for h in hard))

    # -- check 5: mask/register pairing -----------------------------------
    mask_no_register = GOOD_ROW_TMPL.format(
        name="foo on", flag="&bFOO", sset="nullptr",
        and_mask="0xFFFFFFFF", or_mask="0x0001", opt="TG_FLAG_TRUE",
    )
    hard, _ = messages(make_table(mask_no_register))
    expect("a non-trivial mask with sset=nullptr is reported",
           any("discarded, no register" in h for h in hard))

    register_no_mask = GOOD_ROW_TMPL.format(
        name="foo on", flag="&bFOO", sset="&meshcom_settings.node_sset",
        and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
    )
    hard, _ = messages(make_table(register_no_mask))
    expect("a real register with a trivial no-op mask is reported",
           any("register with no mask" in h for h in hard))

    # -- check 6: TG_FLAG_TRUE needs a flag ---------------------------------
    flag_true_null = GOOD_ROW_TMPL.format(
        name="foo on", flag="nullptr", sset="&meshcom_settings.node_sset",
        and_mask="0xFFFFFFFF", or_mask="0x0001", opt="TG_FLAG_TRUE",
    )
    hard, _ = messages(make_table(flag_true_null))
    expect("TG_FLAG_TRUE with flag=nullptr is reported",
           any("TG_FLAG_TRUE set but flag is nullptr" in h for h in hard))

    # -- check 7: pair complementarity (warnings only) ----------------------
    broken_pair = (
        GOOD_ROW_TMPL.format(
            name="foo on", flag="&bFOO", sset="&meshcom_settings.node_sset",
            and_mask="0xFFFFFFFF", or_mask="0x0001", opt="TG_FLAG_TRUE",
        )
        + GOOD_ROW_TMPL.format(
            name="foo off", flag="&bFOO", sset="&meshcom_settings.node_sset",
            and_mask="0xFFFFFFFD", or_mask="0x00000000", opt="0",
        )
    )
    hard, warn = messages(make_table(broken_pair))
    expect("a non-complementary on/off pair is a WARNING, not a hard failure",
           hard == [] and any("not bit-complementary" in w for w in warn))

    # 'mesh' is the one pair that DOES trip the check (off.or_mask != 0);
    # it must WARN, never silently pass and never hard-fail.
    mesh_pair = (
        GOOD_ROW_TMPL.format(
            name="mesh on", flag="&bMESH", sset="&meshcom_settings.node_sset2",
            and_mask="0xFFFFFFDF", or_mask="0x00000000", opt="TG_SAVE | TG_FLAG_TRUE",
        )
        + GOOD_ROW_TMPL.format(
            name="mesh off", flag="&bMESH", sset="&meshcom_settings.node_sset2",
            and_mask="0xFFFFFFFF", or_mask="0x0020", opt="TG_SAVE",
        )
    )
    hard, warn = messages(make_table(mesh_pair))
    expect("the documented 'mesh on/off' inversion warns and does not hard-fail",
           hard == [] and any("mesh" in w for w in warn))

    button_pair = (
        GOOD_ROW_TMPL.format(
            name="button on", flag="&bButtonCheck", sset="&meshcom_settings.node_sset",
            and_mask="0xFFFFFFFF", or_mask="0x0010", opt="TG_SAVE | TG_FLAG_TRUE",
        )
        + GOOD_ROW_TMPL.format(
            name="button off", flag="&bButtonCheck", sset="&meshcom_settings.node_sset",
            and_mask="0x7FEF", or_mask="0x00000000", opt="TG_SAVE",
        )
    )
    hard, warn = messages(make_table(button_pair))
    expect("the documented 'button off' second-bit AND-mask clears the target "
           "bit too, so it passes cleanly with no hard failure and no warning",
           hard == [] and warn == [])

    # -- check 8: phone answer ----------------------------------------------
    def via_rows(dirty: str, opt_on: str, opt_off: str) -> str:
        return (
            '    { "--via on", &bVIA, &meshcom_settings.node_sset2, 0xFFFFFFFF, '
            '0x4000, nullptr, %s, %s },\n' % (dirty, opt_on)
            + '    { "--via off", &bVIA, &meshcom_settings.node_sset2, 0xFFFFBFFF, '
            '0x00000000, nullptr, %s, %s },\n' % (dirty, opt_off)
        )

    good_via = via_rows("TG_DIRTY_NODE", "TG_SAVE | TG_BRETURN | TG_FLAG_TRUE",
                        "TG_SAVE | TG_BRETURN")
    hard, _ = messages(make_table(good_via))
    expect("'--via on/off' answering with the node-settings JSON passes",
           not any("phone" in h or "JSON" in h for h in hard))

    # The pre-merge rows: text echo instead of SN + SN1.
    old_via = via_rows("TG_DIRTY_NONE", "TG_SAVE | TG_FLAG_TRUE | TG_BLE_ECHO",
                       "TG_SAVE | TG_BLE_ECHO")
    hard, _ = messages(make_table(old_via))
    expect("'--via on/off' with a text echo instead of the JSON is reported",
           sum("must answer the phone" in h for h in hard) == 2)

    both = GOOD_ROW_TMPL.replace("TG_DIRTY_NONE", "TG_DIRTY_NODE").format(
        name="foo on", flag="&bFOO", sset="nullptr",
        and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE | TG_BLE_ECHO",
    )
    hard, _ = messages(make_table(both))
    expect("TG_BLE_ECHO together with a settings dirty class is reported",
           any("stray text echo" in h for h in hard))

    expect("the real-table mode reports missing pinned '--via' rows",
           any("no such row" in h
               for h in check_phone_answer(parse_rows(make_table(clean_pair)), True)))
    expect("the synthetic-table mode does not require the pinned rows",
           check_phone_answer(parse_rows(make_table(clean_pair)), False) == [])

    # -- guards are recorded but never used to skip rows --------------------
    guarded = (
        "#if defined(ENABLE_FOO)\n"
        + GOOD_ROW_TMPL.format(
            name="Debug on", flag="&bDEBUG", sset="nullptr",
            and_mask="0xFFFFFFFF", or_mask="0x00000000", opt="TG_FLAG_TRUE",
        )
        + "#endif\n"
    )
    rows, hard, _ = run_checks(make_table(guarded), "")
    expect("a row inside a disabled #if guard is still checked",
           len(rows) == 1 and any("Debug on" in h for h in hard))
    expect("the row records its guard for reporting",
           rows[0].guard == "#if defined(ENABLE_FOO)")

    # -- the real tree ------------------------------------------------------
    real_text = COMMANDS.read_text()
    real_table = extract_table_text(real_text)
    real_rows, real_hard, real_warn = run_checks(real_table, real_text,
                                                 require_pinned=True)
    expect(
        "the real tree has 70 rows and no hard failures (%d rows, %d hard, %d warn)"
        % (len(real_rows), len(real_hard), len(real_warn)),
        len(real_rows) == 70 and real_hard == [],
    )
    if real_hard:
        for h in real_hard:
            print("    " + h)

    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    text = COMMANDS.read_text()
    table_text = extract_table_text(text)
    start_lineno = text.count("\n", 0, text.index(TABLE_START)) + 1
    rows, hard, warnings = run_checks(table_text, text, start_lineno=start_lineno,
                                      require_pinned=True)

    if warnings:
        print("toggle table lint: %d pair-complementarity warning(s) "
              "(expected -- see this file's docstring, check 7):" % len(warnings))
        for w in warnings:
            print("  WARN  " + w)

    if hard:
        print("toggle table lint: %d hard failure(s) in COMMAND_TOGGLES[]:" % len(hard))
        for h in hard:
            print("  FAIL  " + h)
        return 1

    print("toggle table lint: %d row(s) checked, 0 hard failures, "
          "%d warning(s)" % (len(rows), len(warnings)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
