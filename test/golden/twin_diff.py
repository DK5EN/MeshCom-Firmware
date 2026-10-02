#!/usr/bin/env python3
"""Before/after gate for the U1/U2 ESP32-vs-nRF52 sink-call twins (testplan
§4.4, §9.1).

`test_udp_frame_twin` and `test_udp_send_twin` (planned, `test/golden/
native/`) each dump, for every corpus item, the ordered sequence of sink
calls the ESP32 code path made and the sequence the nRF52 code path made --
one line per call, `NNN<TAB>SINK<TAB>detail`, grouped into `=== <corpus
filename>` blocks. The two platforms are NOT expected to agree: they are two
different implementations of the same nominal behaviour (that divergence is
exactly what Phase 2's `docs/testplan/drift-matrix.csv` exists to review and
either accept or fix). So the useful artifact is not "the diff between them"
in isolation -- it is that diff CAPTURED, at N1, before the DRY unification
touches anything, and committed as `<uut>-twin-diff-before.txt`.

That is why §9.1's pass condition for the after-run is explicitly **not**
"the diff is empty" -- an empty diff would be false for most blocks even on
an unmodified tree, and chasing it would just mean overwriting real,
pre-existing platform differences until the tool stops complaining. The
actual pass condition is **"the diff recomputed from the after-run dumps
equals the committed before-diff, byte for byte"**. A block that agreed
before and still agrees: fine. A block that differed before in exactly the
same way it differs now (a known, reviewed platform difference nobody
touched): fine. What FAILS the gate is drift that CHANGES -- a block that
used to agree suddenly not agreeing (a regression introduced by the
refactor), or one that used to differ now agreeing (which is not a failure
of the refactor at all -- it is very possibly a decided drift-matrix fix
landing -- but it still means the committed before-diff is stale and must be
regenerated deliberately, not silently absorbed by a gate that only checks
"is it empty").

Modes:
    twin_diff.py --generate <esp32.txt> <nrf52.txt> <out.txt>
        Produce the before-diff (or, re-run later, a fresh comparison) and
        write it to <out.txt>. This is what N1 commits as
        `<uut>-twin-diff-before.txt`.
    twin_diff.py --check <esp32.txt> <nrf52.txt> <committed-diff.txt>
        Recompute the diff from the two CURRENT dumps and compare it to the
        committed one. Exit 0 only on an exact match. On a mismatch, report
        at block granularity, and say plainly which direction each changed
        block moved -- an agree-to-differ block (a regression) reads
        differently from a differ-to-agree block (an expected fix landing),
        because a human deciding whether to regenerate the committed file
        needs to know which one they are looking at.
    twin_diff.py --self-test
        Runs entirely against synthetic in-memory dumps; never reads the
        real `test/golden/native/u*-esp32.txt` / `u*-nrf52.txt` files, which
        are written by other tools and may not exist yet.

Fatal-not-clean discipline (matches `drift_matrix_lint.py`): a missing input
file, a file with zero `=== ` blocks (including an empty file), a file that
contains a line outside the fixed grammar, or a block-count/block-set
mismatch between the two platform dumps is FATAL and exits non-zero with an
explanation -- never a silent "0 blocks, looks clean" pass. `--check`
additionally treats a missing or unparseable committed-diff file as fatal
for the same reason: a broken instrument must never read as a green gate.

INPUT GRAMMAR (fixed contract, both twin-test suites emit it -- see the
testplan and the dispatch brief for the exact shape):

    === <corpus filename>
    NNN<TAB>SINK<TAB>detail
    NNN<TAB>SINK<TAB>detail
    === <next corpus filename>
    (no sink calls)
    === <next corpus filename>
    (not applicable: <reason>)

`NNN` is a zero-padded 3-digit index restarting at 001 within each block and
must be strictly sequential (a hole or a duplicate is unparseable, not a
gap). `(no sink calls)` and `(not applicable: <reason>)` are each, when
present, the SOLE line of their block -- a marker line does not mix with
call lines in the same block.
"""
import argparse
import difflib
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

NO_SINK_CALLS_MARKER = "(no sink calls)"
NOT_APPLICABLE_RE = re.compile(r"^\(not applicable: (.+)\)$")
CALL_INDEX_RE = re.compile(r"^\d{3}$")
SINK_TOKEN_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

# Header line format this tool itself writes into a rendered diff document
# (--generate output / committed-diff input to --check). This is OUR format,
# not the input-dump grammar above -- parse_diff_document() below is the only
# reader of it.
DOC_HEADER_RE = re.compile(r"^=== (.+?): (identical \(.*\)|DIFFERS.*)$")


class Fatal(Exception):
    """A condition that must never look like a clean pass -- see the module
    docstring's Fatal-not-clean-discipline paragraph."""


@dataclass
class BlockResult:
    name: str
    status: str  # "agree" | "differ"
    lines: List[str]  # rendered header + body, ready to join into the document


def parse_call_line(line: str) -> Tuple[str, str, str]:
    """Return (idx, sink, detail) if `line` is a well-formed call line, else
    raise ValueError. `detail` is everything after the second tab, verbatim
    (it may itself contain tabs, and may be empty)."""
    parts = line.split("\t")
    if len(parts) < 3:
        raise ValueError("fewer than 3 tab-separated fields")
    idx, sink = parts[0], parts[1]
    detail = "\t".join(parts[2:])
    if not CALL_INDEX_RE.match(idx):
        raise ValueError(f"call index {idx!r} is not 3 digits")
    if not SINK_TOKEN_RE.match(sink):
        raise ValueError(f"sink token {sink!r} is not an uppercase identifier")
    return idx, sink, detail


def parse_dump_text(text: str, label: str) -> Dict[str, List[str]]:
    """Parse one platform dump against the fixed input grammar. Returns
    {corpus filename: [rendered call/marker lines]} in file order. Raises
    Fatal on anything outside the grammar -- see the module docstring."""
    blocks: Dict[str, List[str]] = {}
    name: str = None
    lines: List[str] = []
    expected_idx = 0
    marker_seen = False

    def close_block() -> None:
        nonlocal name, lines, expected_idx, marker_seen
        if name is not None:
            if name in blocks:
                raise Fatal(f"{label}: duplicate block '=== {name}'")
            blocks[name] = lines
        name, lines, expected_idx, marker_seen = None, [], 0, False

    for lineno, raw in enumerate(text.splitlines(), start=1):
        if raw.startswith("=== "):
            close_block()
            name = raw[4:].strip()
            if not name:
                raise Fatal(f"{label}:{lineno}: '=== ' header names no corpus file")
            continue
        if not raw.strip():
            continue  # blank separator line -- ignored, not significant
        if name is None:
            raise Fatal(f"{label}:{lineno}: content before the first '=== ' "
                        f"block header: {raw!r}")
        if raw == NO_SINK_CALLS_MARKER:
            if lines or marker_seen:
                raise Fatal(f"{label}:{lineno}: '{NO_SINK_CALLS_MARKER}' must "
                            f"be the only line in its block")
            lines.append(raw)
            marker_seen = True
            continue
        na = NOT_APPLICABLE_RE.match(raw)
        if na:
            if lines or marker_seen:
                raise Fatal(f"{label}:{lineno}: '(not applicable: ...)' must "
                            f"be the only line in its block")
            lines.append(raw)
            marker_seen = True
            continue
        if marker_seen:
            raise Fatal(f"{label}:{lineno}: a call line follows a marker "
                        f"line ('{NO_SINK_CALLS_MARKER}' or '(not "
                        f"applicable: ...)') in the same block")
        try:
            idx, sink, detail = parse_call_line(raw)
        except ValueError as e:
            raise Fatal(f"{label}:{lineno}: unparseable line ({e}): {raw!r}")
        expected_idx += 1
        want = f"{expected_idx:03d}"
        if idx != want:
            raise Fatal(f"{label}:{lineno}: call index out of sequence -- "
                        f"expected {want}, got {idx}")
        lines.append(f"{idx}\t{sink}\t{detail}")
    close_block()

    if not blocks:
        raise Fatal(f"{label}: no '=== ' block headers found -- file is "
                     f"empty or does not follow the twin-dump grammar at all")
    return blocks


def parse_dump(path: Path) -> Dict[str, List[str]]:
    if not path.exists():
        raise Fatal(f"{path}: file does not exist")
    return parse_dump_text(path.read_text(), str(path))


def load_pair(esp32_path: Path, nrf52_path: Path) -> Tuple[Dict[str, List[str]], Dict[str, List[str]]]:
    """Parse both platform dumps and validate they describe the same set of
    corpus blocks. A block-count or block-set mismatch is fatal: with no
    block correspondence there is nothing meaningful to diff."""
    esp32_blocks = parse_dump(esp32_path)
    nrf52_blocks = parse_dump(nrf52_path)
    if len(esp32_blocks) != len(nrf52_blocks):
        raise Fatal(f"block count mismatch: {esp32_path} has "
                    f"{len(esp32_blocks)} block(s), {nrf52_path} has "
                    f"{len(nrf52_blocks)} block(s)")
    esp32_names, nrf52_names = set(esp32_blocks), set(nrf52_blocks)
    if esp32_names != nrf52_names:
        only_esp32 = sorted(esp32_names - nrf52_names)
        only_nrf52 = sorted(nrf52_names - esp32_names)
        raise Fatal(f"block set mismatch between {esp32_path} and "
                    f"{nrf52_path} (same count, different names): only in "
                    f"esp32={only_esp32}, only in nrf52={only_nrf52}")
    return esp32_blocks, nrf52_blocks


def summarize(lines: List[str]) -> str:
    if lines == [NO_SINK_CALLS_MARKER]:
        return "no sink calls"
    if len(lines) == 1:
        na = NOT_APPLICABLE_RE.match(lines[0])
        if na:
            return f"not applicable: {na.group(1)}"
    return f"{len(lines)} sink call(s)"


def strip_call_index(line: str) -> str:
    """Drop the leading `NNN<TAB>` call index for COMPARISON purposes only.

    The index restarts at 001 within each block, so it encodes position --
    which the line's own position in the list already encodes. Comparing it
    too makes the diff pathologically position-sensitive: ONE extra call at
    the top of a block renumbers every line below it, and a single inserted
    sink then renders as "every following line differs". That is exactly what
    happened on the first real run against u1: `01-gate-f001.hex` differs only
    by an ESP32-only DISPLAYPOS, but the raw index comparison reported all
    three following lines as changed too, with a '?' char-hint on each.

    Ordering is still fully compared -- the lines are diffed as an ordered
    sequence, so a reorder is still a difference. Only the redundant printed
    counter is normalised away. The indices stay in the OUTPUT, because a
    human reading the artefact wants them.
    """
    parts = line.split("\t")
    if len(parts) >= 2 and CALL_INDEX_RE.match(parts[0]):
        return "\t".join(parts[1:])
    return line


def diff_block(name: str, esp32_lines: List[str], nrf52_lines: List[str]) -> BlockResult:
    """One block's rendering: a single 'identical' line when the two
    platforms produced the same sequence (order included -- an ordered log
    that merely reorders is NOT identical), else a header plus an ndiff body
    that shows extra/missing/reordered/detail-changed calls line by line.

    Comparison ignores the printed call index; see strip_call_index()."""
    if [strip_call_index(l) for l in esp32_lines] == [strip_call_index(l) for l in nrf52_lines]:
        return BlockResult(name, "agree",
                            [f"=== {name}: identical ({summarize(esp32_lines)})"])
    header = (f"=== {name}: DIFFERS  (esp32 {len(esp32_lines)} line(s), "
              f"nrf52 {len(nrf52_lines)} line(s); '-' esp32-only, "
              f"'+' nrf52-only, '?' char-level hint)")
    body = [f"    {l}" for l in difflib.ndiff(
        [strip_call_index(l) for l in esp32_lines],
        [strip_call_index(l) for l in nrf52_lines])]
    return BlockResult(name, "differ", [header] + body)


def compute_blocks(esp32_blocks: Dict[str, List[str]],
                    nrf52_blocks: Dict[str, List[str]]) -> List[BlockResult]:
    """Sorted by corpus filename regardless of input file order, so the
    rendering is stable and diffable independent of how the dumps were
    written (the input grammar already promises sorted order; this makes
    that a guarantee of the tool, not an assumption about its inputs)."""
    return [diff_block(name, esp32_blocks[name], nrf52_blocks[name])
            for name in sorted(esp32_blocks)]


def render_document(results: List[BlockResult]) -> str:
    """The committed before-diff. No timestamps, no absolute paths, no
    run-dependent ordering -- content alone decides the bytes, so two runs
    over the same dumps always produce the same file, and --check's
    byte-exact comparison means something."""
    agree_n = sum(1 for r in results if r.status == "agree")
    differ_n = len(results) - agree_n
    header = [
        "# twin-diff: ordered sink-call sequence comparison, ESP32 vs nRF52",
        "# (testplan sec 4.4 / sec 9.1). Pass condition for --check is NOT an",
        "# empty diff -- it is this file staying byte-identical to a fresh",
        "# --generate run. The drift recorded below is expected; --check",
        "# catches drift that CHANGES (a block flipping agree<->differ).",
        f"# {len(results)} block(s): {agree_n} agree, {differ_n} differ",
        "",
    ]
    body: List[str] = []
    for r in results:
        body.extend(r.lines)
    return "\n".join(header + body) + "\n"


def parse_diff_document(text: str, label: str) -> Dict[str, Tuple[str, str]]:
    """Parse a rendered diff document (this tool's own output format, as
    read back from a committed-diff file) into {block name: (status, full
    block text)}. Used only by --check, to recover what the committed
    before-diff said about each block."""
    blocks: Dict[str, Tuple[str, str]] = {}
    name = None
    status = None
    chunk: List[str] = []

    def close() -> None:
        nonlocal name, status, chunk
        if name is not None:
            if name in blocks:
                raise Fatal(f"{label}: duplicate block '{name}' in "
                            f"committed diff")
            blocks[name] = (status, "\n".join(chunk))
        name, status, chunk = None, None, []

    for raw in text.splitlines():
        m = DOC_HEADER_RE.match(raw)
        if m:
            close()
            name = m.group(1)
            status = "agree" if m.group(2).startswith("identical") else "differ"
            chunk = [raw]
            continue
        if name is None:
            continue  # a leading '#' comment line, or a blank line
        chunk.append(raw)
    close()

    if not blocks:
        raise Fatal(f"{label}: no block sections found -- not a twin-diff "
                    f"document (empty, corrupted, or the wrong file)")
    return blocks


def do_generate(esp32_path: str, nrf52_path: str, out_path: str) -> int:
    esp32_blocks, nrf52_blocks = load_pair(Path(esp32_path), Path(nrf52_path))
    results = compute_blocks(esp32_blocks, nrf52_blocks)
    text = render_document(results)
    Path(out_path).write_text(text)
    agree_n = sum(1 for r in results if r.status == "agree")
    print(f"wrote {out_path}: {len(results)} block(s), {agree_n} agree, "
          f"{len(results) - agree_n} differ")
    return 0


def do_check(esp32_path: str, nrf52_path: str, committed_path: str) -> int:
    esp32_blocks, nrf52_blocks = load_pair(Path(esp32_path), Path(nrf52_path))
    current_results = compute_blocks(esp32_blocks, nrf52_blocks)
    current_text = render_document(current_results)

    cp = Path(committed_path)
    if not cp.exists():
        raise Fatal(f"{cp}: committed diff file does not exist")
    committed_text = cp.read_text()
    if not committed_text.strip():
        raise Fatal(f"{cp}: committed diff file is empty")

    agree_n = sum(1 for r in current_results if r.status == "agree")
    if current_text == committed_text:
        print(f"OK: matches the committed diff exactly ({len(current_results)} "
              f"block(s): {agree_n} agree, {len(current_results) - agree_n} differ)")
        return 0

    committed_blocks = parse_diff_document(committed_text, str(cp))
    current_map = {r.name: (r.status, "\n".join(r.lines)) for r in current_results}

    changes: List[str] = []
    for nm in sorted(set(committed_blocks) | set(current_map)):
        in_committed = nm in committed_blocks
        in_current = nm in current_map
        if not in_committed:
            changes.append(f"NEW {nm}: present now, absent from the "
                            f"committed diff -- corpus item added since "
                            f"--generate; re-run --generate to update it")
            continue
        if not in_current:
            changes.append(f"REMOVED {nm}: present in the committed diff, "
                            f"absent now -- corpus item removed or renamed "
                            f"since --generate")
            continue
        c_status, c_text = committed_blocks[nm]
        n_status, n_text = current_map[nm]
        if c_text == n_text:
            continue
        if c_status == "agree" and n_status == "differ":
            changes.append(f"REGRESSION {nm}: used to AGREE, now DIFFERS -- "
                            f"ESP32 and nRF52 have diverged since the "
                            f"committed before-diff; look at this corpus item")
        elif c_status == "differ" and n_status == "agree":
            changes.append(f"FIXED {nm}: used to DIFFER, now AGREES -- an "
                            f"expected drift row was resolved; if "
                            f"intentional, re-run --generate to update the "
                            f"committed diff")
        else:
            word = "agrees" if n_status == "agree" else "differs"
            changes.append(f"CHANGED {nm}: still {word} on both platforms, "
                            f"but the recorded detail changed -- re-examine "
                            f"this corpus item")

    print(f"FAIL: current diff does not match {committed_path} "
          f"({len(changes)} block(s) changed)")
    if not changes:
        print("  (no single block differs -- the mismatch is only in the "
              "document header/comment lines; the file content still "
              "differs byte-for-byte)")
    for c in changes:
        print(f"  {c}")
    return 1


def _write(d: Path, name: str, text: str) -> Path:
    p = d / name
    p.write_text(text)
    return p


def self_test() -> int:
    ok = True

    def report(good: bool, label: str, extra: str = "") -> None:
        nonlocal ok
        ok = ok and good
        suffix = f": {extra}" if extra else ""
        print(f"  {'ok ' if good else 'FAIL'} {label}{suffix}")

    # --- case 1: identical dumps -- every block agrees ---
    dump = ("=== a.bin\n001\tSINK_A\tfoo\n002\tSINK_B\tbar\n"
            "=== b.bin\n(no sink calls)\n"
            "=== c.bin\n(not applicable: no path in this corpus)\n")
    esp32 = parse_dump_text(dump, "esp32")
    nrf52 = parse_dump_text(dump, "nrf52")
    results = compute_blocks(esp32, nrf52)
    good = len(results) == 3 and all(r.status == "agree" for r in results)
    report(good, "identical dumps -> every block agrees",
           f"statuses={[r.status for r in results]}")

    # --- case 2: extra sink call on one side ---
    esp32_2 = parse_dump_text("=== a.bin\n001\tSINK_A\tfoo\n", "esp32")
    nrf52_2 = parse_dump_text(
        "=== a.bin\n001\tSINK_A\tfoo\n002\tSINK_B\tbar\n", "nrf52")
    r = diff_block("a.bin", esp32_2["a.bin"], nrf52_2["a.bin"])
    good = (r.status == "differ"
            and any("+" in l and "SINK_B" in l for l in r.lines))
    report(good, "extra sink call on one side -> differ, extra call shown")

    # --- case 3: differ only in a detail field ---
    esp32_3 = parse_dump_text("=== a.bin\n001\tSINK_A\tmsg_id=5\n", "esp32")
    nrf52_3 = parse_dump_text("=== a.bin\n001\tSINK_A\tmsg_id=6\n", "nrf52")
    r = diff_block("a.bin", esp32_3["a.bin"], nrf52_3["a.bin"])
    good = r.status == "differ" and any("msg_id=5" in l for l in r.lines) \
        and any("msg_id=6" in l for l in r.lines)
    report(good, "detail-only difference -> differ, both details shown")

    # --- case 4: same calls, different order -> must be reported as differ ---
    esp32_4 = parse_dump_text(
        "=== a.bin\n001\tSINK_A\tx\n002\tSINK_B\ty\n", "esp32")
    nrf52_4 = parse_dump_text(
        "=== a.bin\n001\tSINK_B\ty\n002\tSINK_A\tx\n", "nrf52")
    r = diff_block("a.bin", esp32_4["a.bin"], nrf52_4["a.bin"])
    good = r.status == "differ"
    report(good, "same call set, different order -> differ (order is the point)")

    # --- case 5: '(no sink calls)' on one side only ---
    esp32_5 = parse_dump_text("=== a.bin\n(no sink calls)\n", "esp32")
    nrf52_5 = parse_dump_text("=== a.bin\n001\tSINK_A\tx\n", "nrf52")
    r = diff_block("a.bin", esp32_5["a.bin"], nrf52_5["a.bin"])
    good = r.status == "differ"
    report(good, "'(no sink calls)' on one side only -> differ")

    # --- case 6: '(not applicable: ...)' block, agreeing and differing ---
    na_same = parse_dump_text(
        "=== a.bin\n(not applicable: no path in this corpus)\n", "x")
    r = diff_block("a.bin", na_same["a.bin"], na_same["a.bin"])
    good = r.status == "agree" and "not applicable" in r.lines[0]
    report(good, "matching '(not applicable: ...)' block -> agree")

    na_a = parse_dump_text("=== a.bin\n(not applicable: reason one)\n", "x")
    na_b = parse_dump_text("=== a.bin\n(not applicable: reason two)\n", "x")
    r = diff_block("a.bin", na_a["a.bin"], na_b["a.bin"])
    good = r.status == "differ"
    report(good, "'(not applicable: ...)' with different reasons -> differ")

    # --- end-to-end: --generate then --check, using real temp files ---
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        esp32_before = _write(d, "esp32-before.txt",
            "=== a.bin\n001\tSINK_A\tx\n"
            "=== b.bin\n001\tSINK_C\tp\n002\tSINK_D\tq\n"
            "=== c.bin\n(no sink calls)\n")
        nrf52_before = _write(d, "nrf52-before.txt",
            "=== a.bin\n001\tSINK_A\tx\n"          # agrees
            "=== b.bin\n001\tSINK_C\tp\n"          # differs (missing 002)
            "=== c.bin\n(no sink calls)\n")        # agrees
        committed = d / "committed.txt"
        rc = do_generate(str(esp32_before), str(nrf52_before), str(committed))
        good = rc == 0 and committed.exists()
        report(good, "--generate produces a committed diff file")

        # --- case 7: --check against a matching committed diff -> exit 0 ---
        rc = do_check(str(esp32_before), str(nrf52_before), str(committed))
        report(rc == 0, "--check against unchanged dumps -> exit 0")

        # regenerating from the same dumps must be byte-identical (no
        # run-dependent ordering / timestamps leaking into the rendering)
        committed2 = d / "committed2.txt"
        do_generate(str(esp32_before), str(nrf52_before), str(committed2))
        good = committed.read_text() == committed2.read_text()
        report(good, "--generate is deterministic across repeated runs")

        # --- case 8: a block flips agree -> differ (regression) ---
        nrf52_regressed = _write(d, "nrf52-regressed.txt",
            "=== a.bin\n001\tSINK_A\tx\tCHANGED\n"  # was agree, now differs
            "=== b.bin\n001\tSINK_C\tp\n"
            "=== c.bin\n(no sink calls)\n")
        rc = do_check(str(esp32_before), str(nrf52_regressed), str(committed))
        report(rc != 0, "--check exits non-zero on an agree->differ flip")

        # --- case 9: a block flips differ -> agree (expected fix landing),
        # distinguished from case 8 ---
        nrf52_fixed = _write(d, "nrf52-fixed.txt",
            "=== a.bin\n001\tSINK_A\tx\n"
            "=== b.bin\n001\tSINK_C\tp\n002\tSINK_D\tq\n"  # now matches esp32
            "=== c.bin\n(no sink calls)\n")
        rc = do_check(str(esp32_before), str(nrf52_fixed), str(committed))
        report(rc != 0, "--check exits non-zero on a differ->agree flip too "
               "(committed file is now stale either way)")

        # capture stdout of both flips to prove they are reported distinctly
        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            do_check(str(esp32_before), str(nrf52_regressed), str(committed))
        regression_out = buf.getvalue()
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            do_check(str(esp32_before), str(nrf52_fixed), str(committed))
        fixed_out = buf.getvalue()
        good = ("REGRESSION a.bin" in regression_out
                and "FIXED a.bin" not in regression_out
                and "FIXED b.bin" in fixed_out
                and "REGRESSION b.bin" not in fixed_out)
        report(good, "agree->differ is labelled REGRESSION, differ->agree is "
               "labelled FIXED, and neither run's output uses the other's word")

        # --- block-count mismatch is fatal, and is a distinct file from
        # the one used above ---
        esp32_extra = _write(d, "esp32-extra.txt",
            "=== a.bin\n001\tSINK_A\tx\n=== d.bin\n001\tSINK_E\tz\n")
        try:
            load_pair(esp32_extra, nrf52_before)
            good = False
        except Fatal as e:
            good = "count mismatch" in str(e)
        report(good, "block count mismatch between the two dumps -> Fatal")

        # --- missing file on disk -> Fatal ---
        try:
            parse_dump(d / "does-not-exist.txt")
            good = False
        except Fatal as e:
            good = "does not exist" in str(e)
        report(good, "missing input file -> Fatal")

    # --- case: empty file -> Fatal, not a silent 0-block clean pass ---
    try:
        parse_dump_text("", "empty")
        good = False
    except Fatal as e:
        good = "no '=== '" in str(e)
    report(good, "empty file -> Fatal (not zero blocks / clean)")

    # --- bonus: a line outside the grammar entirely -> Fatal ---
    try:
        parse_dump_text("=== a.bin\nthis is not a valid line\n", "bad")
        good = False
    except Fatal as e:
        good = "unparseable" in str(e)
    report(good, "[bonus] line outside the grammar -> Fatal, unparseable")

    # --- bonus: call index out of sequence -> Fatal ---
    try:
        parse_dump_text("=== a.bin\n001\tSINK_A\tx\n003\tSINK_B\ty\n", "bad")
        good = False
    except Fatal as e:
        good = "out of sequence" in str(e)
    report(good, "[bonus] call index out of sequence -> Fatal")

    # --- bonus: a marker line sharing a block with a call line -> Fatal ---
    try:
        parse_dump_text("=== a.bin\n(no sink calls)\n001\tSINK_A\tx\n", "bad")
        good = False
    except Fatal as e:
        good = "follows a marker line" in str(e)
    report(good, "[bonus] marker line followed by a call line -> Fatal")

    # --- bonus: same block count, different names -> Fatal ---
    esp32_names = parse_dump_text("=== a.bin\n001\tSINK_A\tx\n", "e")
    nrf52_names = parse_dump_text("=== z.bin\n001\tSINK_A\tx\n", "n")
    try:
        compute_blocks(esp32_names, nrf52_names)  # would KeyError; guard first
        raise AssertionError("compute_blocks must not be reached without load_pair's check")
    except KeyError:
        good = True  # compute_blocks correctly assumes load_pair validated names
    report(good, "[bonus] compute_blocks assumes load_pair's name-set check "
           "(KeyError if bypassed, as expected)")

    print("\ndid not read test/golden/native/u*-esp32.txt or "
          "u*-nrf52.txt anywhere in this self-test")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--generate", nargs=3,
                     metavar=("ESP32_TXT", "NRF52_TXT", "OUT_TXT"),
                     help="write the before-diff of ESP32_TXT vs NRF52_TXT to OUT_TXT")
    ap.add_argument("--check", nargs=3,
                     metavar=("ESP32_TXT", "NRF52_TXT", "COMMITTED_DIFF_TXT"),
                     help="recompute the diff and compare it to COMMITTED_DIFF_TXT; "
                          "exit 0 only on an exact match")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    try:
        if args.generate:
            return do_generate(*args.generate)
        if args.check:
            return do_check(*args.check)
    except Fatal as e:
        print(f"FATAL: {e}")
        return 1

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
