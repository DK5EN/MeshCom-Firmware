#!/usr/bin/env python3
"""Compare two EXTUDP captures (test plan step H8).

The third comparison tool, after `ble_golden.py --compare` and
`compare_udp.py`, and written for the same reason: the surface carries
volatile traffic that will never repeat, so a raw diff says "differs" on a
firmware that did not change, and a hand analysis is not a gate.

**What is comparable: node-originated `msg` datagrams.** Those are the node's
responses to the corpus -- one per corpus entry it accepted -- and they are
deterministic in destination, body and order.

**What is not, with the reason each was found:**

| Class                          | Why it cannot be compared                                |
| ------------------------------ | -------------------------------------------------------- |
| `"src_type":"lora"`            | frames the node heard on the air and forwarded; live      |
|                                | traffic. Measured G0 -> G1: heltec-93 8 -> 3, rak-90 3 -> 0 |
| node `"type":"pos"` / `"tele"` | periodic beacons on their own timers. Whether one fires   |
|                                | inside the capture window is chance: heltec-93 had both   |
|                                | at G0 and neither at G1                                   |
| the `{NNN` suffix on a body    | the node's outgoing message counter, like `msg_id`        |
| `msg_id` itself                | reassigned per capture in order of first appearance by    |
|                                | `normalize.py`, over EVERY id-bearing frame in the file --|
|                                | including `lora`/`pos`. Measured heltec-93: 16 ids in G0  |
|                                | vs 11 in G1, purely from the lora-count difference above, |
|                                | so comparing it verbatim would reintroduce exactly the    |
|                                | false difference this tool exists to avoid                |

With those named, all four bench nodes compared clean between G0 and G1: eight
corpus messages each, identical destinations, bodies and order.

**GLD-01, three gaps in this surface, now closed:**

1. **Truncation.** `extudp_peer.py` used to print `dg.text[:160]` and nothing
   else -- so a capture made by redirecting its console loop (every one of
   G0/G1) never contained anything past character 160, and a position frame
   is ~258 B. Fixed in `extudp_peer.py` (`--record`, a full-fidelity JSON-lines
   sibling file); the console line itself is unchanged on purpose -- a human
   skimming `--listen` output does not want the wall of text, only a capture
   comparison needs it. **G0/G1 stay affected**: they predate `--record` and
   cannot be regenerated from what is committed. This tool tolerates that:
   `dst`/`msg` (this tool's original comparison) sit inside the first 160
   characters on every corpus message ever captured, so they were never
   actually cut -- confirmed by every capture on disk, and self-tested below.
   The *whole*-payload comparison added for gap 3 is truncation-tolerant by
   construction (see `_extract_object`): a datagram whose captured line does
   not parse as a complete JSON object is skipped for that check, counted,
   and reported -- never silently treated as equal, never a false failure.
2. **Source masking.** The peer binds one port and every node with `--extudp
   on` pointed at the host delivers into it -- not hypothetical: one real
   T-Beam capture silently held 8 of another node's 20 datagrams.
   `normalize.py` masks every address to `<ADDR>` before a capture is ever
   committed, so **G0/G1 cannot be checked** -- the information is gone
   before this tool ever sees the file, and nothing here can bring it back.
   Fixed prospectively: `extudp_peer.py --record` now keeps the literal
   source address per datagram. When a capture directory has the resulting
   `extudp-received.jsonl` sibling, this tool groups by address, treats the
   one that sent the most datagrams as the node under test (that alone would
   have separated the T-Beam incident's 12 real from 8 foreign), and drops
   the rest before any comparison runs -- reported, never silent. No sibling
   means the check is skipped with a note, same residual limit as before.
3. **Whole-payload comparison.** Gap 1 is why this used to compare only `msg`
   destinations and bodies. With it addressed, the full payload of each
   corpus response is now compared too (`fw_sub`, `firmware`, `src_type`,
   `snr`/`rssi` after `normalize.py`'s masking, and any field added later) --
   still excluding the volatile classes named above, by the same reasoning.

**DR-18, the one predicted diff (drift-matrix.csv row DR-18; verdict
`docs/testplan/drift-matrix-review-verdict-20260912.md` Finding 4).** Once the
section 6.3 ack JSON (`docs/ack-wer-hat-quittiert.md`) ships via
`queueExtern()`, an EXTUDP capture gains a `{"type":"ack",...}` line the
pre-change baseline does not have. That is the ONE difference this surface is
allowed to show for that change; this tool does not start ignoring ack lines
in general, only recognising the single predicted shape of ONE new line on
the *second* (`b`, "after") capture argument that the first (`a`, "before")
lacks:

- The ack line must parse and match every field section 6.3 actually pins:
  `type` == `"ack"`, `msg_id` an 8-hex-digit string, `status` in `0..2`
  (McApp's currently-accepted range; section 6.3 also mentions a future
  `3..6`, not yet accepted anywhere, so not treated as valid here), `via` in
  `("lora", "udp")`. `from` is explicitly optional in the spec ("wo bekannt,
  sonst weglassen") and unconstrained beyond being a string when present.
  Section 6.3 does NOT say this is a closed key set (it is presented as a
  proposal, "was zu ergaenzen waere", not a schema with "no other keys"), so
  extra fields on the object are not grounds to reject it.
- A line matching `"type":"ack"` that fails that shape check is a hard
  failure on whichever side it is on -- it is not the predicted line, so it
  gets no tolerance.
- Counts, not position: exactly one more well-formed ack line in the capture
  than in the baseline is the predicted diff (reported, not a failure). Zero
  extra is fine (nothing to predict yet). More than one extra, or fewer in
  the capture than the baseline (the baseline had one the capture lost), is
  a failure. This mirrors how this tool already treats the other genuinely
  async classes (lora relays, beacons): counted in bulk, not matched by
  position, because an async gateway ACK's place in the capture relative to
  corpus replay is no more deterministic than theirs.
- **The reverse direction is a different situation, not a mirror-image
  tolerance.** A baseline (`a`) that already carries a well-formed ack line
  is not "the predicted diff" running backwards -- DR-18 predicts an ADDED
  line on the after side, not a pre-existing one. If both sides carry the
  same count of well-formed ack lines (including both having exactly one),
  that is ordinary equality and produces no special note beyond saying so;
  if the baseline has MORE well-formed ack lines than the capture, that is
  treated as an ordinary missing-response regression, same as any other
  disappeared line, and fails.

  python3 test/golden/compare_extudp.py G0/<node>/extudp G1/<node>/extudp
  python3 test/golden/compare_extudp.py --self-test

Exit 0 when the corpus responses match, 1 otherwise.
"""
import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

COUNTER = re.compile(r"\{\d+$")

# DR-18 / section 6.3: a raw-text match, not a parsed one, so it also catches
# a line that fails the shape check below (that line still needs to be found
# and failed on, not silently missed because it never became a Rec).
_ACK_LINE = re.compile(r'"type":"ack"')

# section 6.3 pins these four; `status` 0..2 is McApp's currently-accepted
# range (`normalize_extudp_ack`), not the "3..6 later" the doc floats.
_ACK_MSG_ID = re.compile(r"^[0-9A-Fa-f]{8}$")
_ACK_STATUS_OK = (0, 1, 2)
_ACK_VIA_OK = ("lora", "udp")

# normalize.py substitutes some JSON values with bare `<TOKEN>` text -- e.g.
# `"rssi":<RF>` -- which reads fine to a human but is not valid JSON (a bare
# `<` is not a legal value). Every committed golden already looks like this,
# so the whole-payload comparison below would fail to parse EVERY normalized
# record without this: quoting the token first lets it parse as the string
# "<RF>", which both sides of any comparison carry identically (they went
# through the same normalizer), so no real difference is hidden by doing so.
_BARE_PLACEHOLDER = re.compile(r"(?<=[:,\[])<([A-Za-z0-9]+)>(?=[,\]}])")


def _mask_counter(body: str) -> str:
    return COUNTER.sub("{<CNT>", body)


def _extract_object(line: str) -> Optional[Dict[str, Any]]:
    """The JSON object embedded in one captured line, or None if incomplete.

    Every line here carries some non-JSON prefix (an elapsed time, a source
    address, a byte count -- the exact shape has varied across capture
    generations) and, pre-GLD-01, may have been cut off at 160 characters
    mid-object. `json.JSONDecoder.raw_decode` tells the two apart from a
    single real parse rather than a length heuristic: it either finds one
    complete object starting at the first `{`, or it doesn't. A firmware
    that emitted invalid JSON would fail here too, but this tool already
    trusts these lines enough to regex `dst`/`msg` out of them elsewhere, so
    in practice this only ever fires on a truncated tail.
    """
    quoted = _BARE_PLACEHOLDER.sub(r'"<\1>"', line)
    start = quoted.find("{")
    if start == -1:
        return None
    try:
        obj, _ = json.JSONDecoder().raw_decode(quoted, start)
    except ValueError:
        return None
    return obj if isinstance(obj, dict) else None


def _is_predicted_ack(obj: Dict[str, Any]) -> bool:
    """Whether `obj` matches every field section 6.3 actually pins.

    Deliberately narrow: only `type`/`msg_id`/`status`/`via` are checked
    (`from` is documented optional and otherwise unconstrained), and extra
    keys are not grounds to reject -- section 6.3 is a proposal ("was zu
    ergaenzen waere"), not a closed schema. See the module docstring's DR-18
    section for the reasoning.
    """
    if obj.get("type") != "ack":
        return False
    msg_id = obj.get("msg_id")
    if not (isinstance(msg_id, str) and _ACK_MSG_ID.match(msg_id)):
        return False
    if obj.get("status") not in _ACK_STATUS_OK:
        return False
    if obj.get("via") not in _ACK_VIA_OK:
        return False
    frm = obj.get("from")
    if frm is not None and not isinstance(frm, str):
        return False
    return True


def _check_ack_lines(a_text: str, b_text: str) -> Tuple[List[str], List[str]]:
    """DR-18 (section 6.3): classify every `"type":"ack"` line on both sides.

    Returns (problems, notes). `a_text`/`b_text` are the raw capture files,
    not the filtered `msg`-record lists -- an ack line carries no
    `"src_type"` field at all, so `_record_from_line`'s filter never sees it;
    it has to be found here or not at all.

    Bulk count comparison, not position: see the module docstring for why
    (same reasoning this tool already applies to lora-relay and beacon
    counts). Any line that matches the raw `"type":"ack"` text but fails
    `_is_predicted_ack` is a failure on its own side regardless of counts --
    it is not the predicted line, so nothing here tolerates it.
    """
    problems: List[str] = []
    notes: List[str] = []

    def classify(text: str) -> Tuple[List[Dict[str, Any]], List[str]]:
        valid: List[Dict[str, Any]] = []
        invalid: List[str] = []
        for line in text.splitlines():
            if not _ACK_LINE.search(line):
                continue
            obj = _extract_object(line)
            if obj is not None and _is_predicted_ack(obj):
                valid.append(obj)
            else:
                invalid.append(line.strip())
        return valid, invalid

    a_valid, a_invalid = classify(a_text)
    b_valid, b_invalid = classify(b_text)

    for name, invalid in (("A", a_invalid), ("B", b_invalid)):
        for line in invalid:
            problems.append(
                f"{name} has an ack-type line that does not match the "
                f"section 6.3 contract (type/msg_id/status/via): {line!r}")

    delta = len(b_valid) - len(a_valid)
    if delta == 0:
        if a_valid:
            notes.append(
                f"{len(a_valid)} matching ack line(s) present on both sides "
                f"-- not a new diff, not the DR-18 prediction (that predicts "
                f"an ADDED after-side line), just equal")
    elif delta == 1:
        notes.append(
            "capture (B) has exactly one more well-formed ack line than the "
            "baseline (A) -- the DR-18 (section 6.3) predicted diff, "
            "reported, not a failure")
    elif delta > 1:
        problems.append(
            f"capture (B) has {delta} extra well-formed ack line(s) over "
            f"the baseline (A); only ONE is predicted by DR-18 (section 6.3)")
    else:
        problems.append(
            f"capture (B) is missing {-delta} well-formed ack line(s) that "
            f"the baseline (A) has -- DR-18 predicts an ADDED after-side "
            f"line, not a removed one")

    return problems, notes


def _prepare(obj: Dict[str, Any]) -> Dict[str, Any]:
    """`obj` with the always-volatile fields normalized out (see docstring)."""
    out = dict(obj)
    out.pop("msg_id", None)
    if isinstance(out.get("msg"), str):
        out["msg"] = _mask_counter(out["msg"])
    return out


@dataclass
class Rec:
    """One node `msg` datagram, at whatever fidelity the capture allows.

    `dst`/`body` are always present -- they sit inside the first 160
    characters of every corpus message ever captured (GLD-01 gap 1), so the
    base comparison below never needed the fix to be safe. `obj` is the full
    payload, msg_id-stripped and counter-masked, when the line parsed whole;
    None when it did not (a pre-GLD-01 truncated capture) -- callers must
    skip the whole-payload check for that record, not fail it.
    """

    dst: str
    body: str
    obj: Optional[Dict[str, Any]]


def _record_from_line(line: str) -> Optional[Rec]:
    if '"src_type":"node"' not in line or '"type":"msg"' not in line:
        return None
    dst = re.search(r'"dst":"([^"]*)"', line)
    body = re.search(r'"msg":"([^"]*)"', line)
    masked_body = _mask_counter(body.group(1)) if body else ""
    obj = _extract_object(line)
    return Rec(dst.group(1) if dst else "?", masked_body,
               _prepare(obj) if obj is not None else None)


def _records_from_text(path: Path) -> List[Rec]:
    """The node's own `msg` datagrams, in order, from a console-style capture."""
    out: List[Rec] = []
    for line in path.read_text().splitlines():
        rec = _record_from_line(line)
        if rec is not None:
            out.append(rec)
    return out


def _records_from_jsonl(path: Path) -> Tuple[List[Rec], int]:
    """The same, from a `--record` capture -- full-fidelity, foreign-filtered.

    Returns (records, foreign_count). Every `text` here is complete --
    `--record` never truncates (GLD-01 gap 1) -- so `obj` is never None.
    Grouping by `addr` before filtering to `msg` records closes gap 2: the
    address that sent the most datagrams is the node under test (the split
    that would have separated the T-Beam incident's 12 real from 8 foreign).
    """
    entries: List[Dict[str, Any]] = []
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        try:
            entries.append(json.loads(line))
        except ValueError:
            continue

    counts: Dict[str, int] = {}
    for e in entries:
        counts[e.get("addr", "?")] = counts.get(e.get("addr", "?"), 0) + 1
    primary = max(counts, key=lambda a: counts[a]) if counts else None
    foreign = sum(1 for e in entries if e.get("addr") != primary)

    out: List[Rec] = []
    for e in entries:
        if e.get("addr") != primary:
            continue
        rec = _record_from_line(e.get("text", ""))
        if rec is not None:
            out.append(rec)
    return out, foreign


def load_records(txt_path: Path) -> Tuple[List[Rec], Optional[int]]:
    """Records for one capture dir, preferring a `--record` JSONL sibling.

    Returns (records, foreign_count). foreign_count is None when cross-
    contamination could not be checked at all (GLD-01 gap 2): no sibling,
    either because the capture predates `--record` (true of G0 and G1) or
    was made without it -- the source address was already masked to
    `<ADDR>` by `normalize.py` before this tool ever saw the file.
    """
    jsonl = txt_path.with_suffix(".jsonl")
    if jsonl.exists():
        return _records_from_jsonl(jsonl)
    return _records_from_text(txt_path), None


def volatile_counts(path: Path) -> Tuple[int, int]:
    text = path.read_text()
    lora = text.count('"src_type":"lora"')
    beacons = sum(1 for l in text.splitlines()
                  if '"src_type":"node"' in l
                  and ('"type":"pos"' in l or '"type":"tele"' in l))
    return lora, beacons


def compare(a_dir: Path, b_dir: Path) -> List[str]:
    a_f, b_f = a_dir / "extudp-received.txt", b_dir / "extudp-received.txt"
    for p in (a_f, b_f):
        if not p.exists():
            return [f"missing capture: {p}"]

    a, a_foreign = load_records(a_f)
    b, b_foreign = load_records(b_f)
    problems: List[str] = []

    if len(a) != len(b):
        problems.append(
            f"corpus response count differs: {len(a)} vs {len(b)}")

    skipped = 0
    for i, (x, y) in enumerate(zip(a, b)):
        if (x.dst, x.body) != (y.dst, y.body):
            problems.append(
                f"corpus response {i} differs:\n"
                f"    A dst={x.dst!r} msg={x.body!r}\n"
                f"    B dst={y.dst!r} msg={y.body!r}")
        elif x.obj is None or y.obj is None:
            skipped += 1
        elif x.obj != y.obj:
            problems.append(
                f"corpus response {i} matches on dst/msg but differs "
                f"elsewhere in the payload (GLD-01 gap 3):\n"
                f"    A {x.obj!r}\n"
                f"    B {y.obj!r}")
        if len(problems) >= 6:
            problems.append("    (further differences not listed)")
            break

    al, ab = volatile_counts(a_f)
    bl, bb = volatile_counts(b_f)
    print(f"A {len(a)} corpus responses, {al} relayed, {ab} beacons")
    print(f"B {len(b)} corpus responses, {bl} relayed, {bb} beacons")
    if (al, ab) != (bl, bb):
        print("note: relayed/beacon counts differ -- not a failure, both are "
              "live traffic and timers")

    # DR-18 / section 6.3: on the raw capture text, not the msg-filtered
    # records above -- an ack line carries no "src_type" field, so it would
    # never reach `a`/`b` otherwise. Scope note: unlike the msg records,
    # this does not go through the --record jsonl's per-address foreign
    # filter (GLD-01 gap 2) -- there is no ack traffic in any capture on
    # disk today to have exercised that path, so it is simply unhandled,
    # not silently trusted.
    ack_problems, ack_notes = _check_ack_lines(a_f.read_text(), b_f.read_text())
    problems.extend(ack_problems)
    for note in ack_notes:
        print(f"note: {note}")

    if skipped:
        print(f"note: {skipped} response(s) matched on dst/msg but could not "
              f"be checked further -- captured pre-GLD-01, cut off before "
              f"the closing brace (gap 1)")
    if not problems:
        print(f"corpus responses identical ({len(a)})")

    for name, foreign in (("A", a_foreign), ("B", b_foreign)):
        if foreign is None:
            print(f"note: {name} has no --record capture alongside it -- "
                  f"cross-contamination cannot be checked (GLD-01 gap 2): "
                  f"the source address is already masked to <ADDR> by "
                  f"normalize.py by the time this tool sees the file")
        elif foreign:
            print(f"note: {name} filtered {foreign} datagram(s) from a "
                  f"source other than the node under test before any "
                  f"comparison ran -- cross-contamination on the shared "
                  f"EXTUDP port (GLD-01 gap 2), not a firmware difference")

    # the guard that had to exist after the broadcast incident of 2026-09-11:
    # check what the node actually emitted, never the corpus text
    for name, recs in (("A", a), ("B", b)):
        star = [r for r in recs if r.dst == "*"]
        if star:
            problems.append(
                f"BROADCAST in {name}: {len(star)} response(s) addressed to "
                f"'*' -- bench traffic goes to group 9/9999 or a direct "
                f"contact, never '*'")
    return problems


def self_test() -> int:
    import tempfile
    ok = True
    node = lambda t, dst, msg, extra="": (
        f'<ADDR>  150 {{"src_type":"node","type":"{t}",'
        f'"src":"DK5EN-92","dst":"{dst}","msg":"{msg}"{extra}}}')
    lora = ('<ADDR>  152 {"src_type":"lora","type":"msg",'
            '"src":"DK5EN-92,DK5EN-98","dst":"9999","msg":"an alle"}')
    beacon = ('<ADDR>  258 {"src_type":"node","type":"pos",'
              '"src":"DK5EN-92","msg":""}')

    base = "\n".join([node("msg", "DK5EN-1", "Test 1 2 3{674"),
                      node("msg", "9999", "an alle"), beacon, lora]) + "\n"
    # same responses, different live traffic and no beacon: must pass
    quiet = "\n".join([node("msg", "DK5EN-1", "Test 1 2 3{881"),
                       node("msg", "9999", "an alle")]) + "\n"
    # a response body changed: must fail
    changed = base.replace('"an alle"', '"etwas anderes"')
    # a response missing: must fail
    missing = "\n".join([node("msg", "DK5EN-1", "Test 1 2 3{674"), lora]) + "\n"
    # a broadcast: must fail
    broadcast = base.replace('"dst":"9999"', '"dst":"*"')

    # GLD-01 gap 1: same dst/msg, but B's line is cut mid-object the way
    # extudp_peer.py:327 used to (pre-GLD-01) -- must still pass, not be
    # silently treated as a full match either (checked via captured stdout).
    full_line = node("msg", "DK5EN-1", "Test 1 2 3{674")
    cut_at = full_line.index('"msg":"Test 1 2 3{674"') + len('"msg":"Test 1 2 3{674"')
    truncated_line = full_line[:cut_at]   # no msg_id/firmware/closing brace
    gap1_truncated = "\n".join([truncated_line, node("msg", "9999", "an alle")]) + "\n"

    # GLD-01 gap 3: dst/msg identical, fw_sub differs -- pre-GLD-01 this tool
    # only compared dst/msg and would have missed it; now it must fail.
    fw_t = node("msg", "DK5EN-1", "Test 1 2 3{674", extra=',"fw_sub":"t"')
    fw_s = node("msg", "DK5EN-1", "Test 1 2 3{674", extra=',"fw_sub":"s"')
    gap3_base = "\n".join([fw_t, node("msg", "9999", "an alle")]) + "\n"
    gap3_diff_field = "\n".join([fw_s, node("msg", "9999", "an alle")]) + "\n"

    with tempfile.TemporaryDirectory() as d:
        def mk(name, text):
            p = Path(d) / name
            p.mkdir()
            (p / "extudp-received.txt").write_text(text)
            return p

        def mk_jsonl(name, txt_text, jsonl_entries):
            p = mk(name, txt_text)
            lines = [json.dumps(e) for e in jsonl_entries]
            (p / "extudp-received.jsonl").write_text(
                "\n".join(lines) + ("\n" if lines else ""))
            return p

        a = mk("a", base)
        for name, text, want_ok in (("quiet-run", quiet, True),
                                    ("changed-body", changed, False),
                                    ("missing-response", missing, False),
                                    ("broadcast", broadcast, False)):
            problems = compare(a, mk(name, text))
            good = (not problems) if want_ok else bool(problems)
            print(f"  {'ok ' if good else 'FAIL'} {name}: {len(problems)} "
                  f"problem(s), expected {'none' if want_ok else 'some'}")
            ok = ok and good

        # gap 1: truncated-vs-full must not produce a false difference
        problems = compare(mk("gap1-a", base), mk("gap1-b", gap1_truncated))
        good = not problems
        print(f"  {'ok ' if good else 'FAIL'} gap1-truncated-vs-full: "
              f"{len(problems)} problem(s), expected none")
        ok = ok and good

        # gap 3: widened whole-payload comparison must catch a field that
        # dst/msg alone would have missed
        problems = compare(mk("gap3-a", gap3_base), mk("gap3-b", gap3_diff_field))
        good = bool(problems)
        print(f"  {'ok ' if good else 'FAIL'} gap3-widened-field-diff: "
              f"{len(problems)} problem(s), expected some")
        ok = ok and good

        # gap 2: a --record sibling lets compare() detect and drop a foreign
        # node's datagrams -- including one addressed to "*", which would
        # otherwise trip the BROADCAST guard for traffic that was never ours
        clean_entries = [
            {"addr": "10.0.0.5", "text": node("msg", "DK5EN-1", "Test 1 2 3{674")},
            {"addr": "10.0.0.5", "text": node("msg", "9999", "an alle")},
        ]
        contaminated_entries = clean_entries + [
            {"addr": "10.0.0.9", "text": node("msg", "OTHER-1", "fremd{1")},
            {"addr": "10.0.0.9", "text": node("msg", "*", "fremd broadcast{2")},
        ]
        clean = mk_jsonl("gap2-clean", base, clean_entries)
        contaminated = mk_jsonl("gap2-contaminated", base, contaminated_entries)
        problems = compare(clean, contaminated)
        good = not problems
        print(f"  {'ok ' if good else 'FAIL'} gap2-cross-contamination: "
              f"{len(problems)} problem(s), expected none (foreign traffic "
              f"filtered before comparison)")
        ok = ok and good

        # DR-18 / section 6.3: the ack line drift-matrix.csv predicts once
        # the ack JSON ships. `ack_line()` builds a raw capture line the
        # same shape a real one would have (some prefix, then the object);
        # the prefix content itself is never inspected, only `"type":"ack"`.
        def ack_line(msg_id="1A2B3C4D", status=1, via="lora", frm=None):
            obj = {"type": "ack", "msg_id": msg_id, "status": status}
            if frm is not None:
                obj["from"] = frm
            obj["via"] = via
            # separators=(",", ":"): every real capture line on disk is
            # compact JSON with no space after ":" -- `_ACK_LINE` and
            # `_record_from_line`'s substring checks both key on that.
            return f"<ADDR>   90 {json.dumps(obj, separators=(',', ':'))}"

        ack1 = ack_line()
        base_plus_ack = base + ack1 + "\n"

        # baseline == capture, clean: the required first case, spelled out
        # for the ack path even though the plain `a`-vs-`a` shape is already
        # implied by every "must pass" case above.
        problems = compare(mk("ack-clean-a", base), mk("ack-clean-b", base))
        good = not problems
        print(f"  {'ok ' if good else 'FAIL'} ack-baseline-equals-capture: "
              f"{len(problems)} problem(s), expected none")
        ok = ok and good

        # capture has the ONE predicted ack line: clean, reported as
        # predicted (checked via captured stdout, like gap 1 above).
        problems = compare(mk("ack-pred-a", base), mk("ack-pred-b", base_plus_ack))
        good = not problems
        print(f"  {'ok ' if good else 'FAIL'} ack-predicted-diff: "
              f"{len(problems)} problem(s), expected none")
        ok = ok and good

        # capture has an unexpected EXTRA ack line (two, not the one
        # predicted): must fail.
        base_plus_two_acks = (base + ack1 + "\n" +
                               ack_line(msg_id="2B3C4D5E") + "\n")
        problems = compare(mk("ack-extra-a", base),
                            mk("ack-extra-b", base_plus_two_acks))
        good = bool(problems)
        print(f"  {'ok ' if good else 'FAIL'} ack-unexpected-extra-line: "
              f"{len(problems)} problem(s), expected some")
        ok = ok and good

        # an ack line in the wrong position -- the baseline has it and the
        # capture does not (a regression, not the after-side addition DR-18
        # predicts): must fail, same bucket as "capture is missing a line
        # the baseline has".
        problems = compare(mk("ack-lost-a", base_plus_ack), mk("ack-lost-b", base))
        good = bool(problems)
        print(f"  {'ok ' if good else 'FAIL'} ack-missing-from-capture: "
              f"{len(problems)} problem(s), expected some")
        ok = ok and good

        # both sides already carry the same ack line: the "different
        # situation" the module docstring calls out -- ordinary equality,
        # not the predicted-diff path, and not a failure either.
        problems = compare(mk("ack-both-a", base_plus_ack), mk("ack-both-b", base_plus_ack))
        good = not problems
        print(f"  {'ok ' if good else 'FAIL'} ack-already-on-both-sides: "
              f"{len(problems)} problem(s), expected none")
        ok = ok and good

        # a malformed ack (status outside 0..2): must fail even though it is
        # the only ack line and even though it is on the after side -- shape
        # is checked before count/direction.
        base_plus_bad_ack = base + ack_line(status=9) + "\n"
        problems = compare(mk("ack-malformed-a", base),
                            mk("ack-malformed-b", base_plus_bad_ack))
        good = bool(problems)
        print(f"  {'ok ' if good else 'FAIL'} ack-malformed-shape: "
              f"{len(problems)} problem(s), expected some")
        ok = ok and good

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", nargs="?", type=Path)
    ap.add_argument("b", nargs="?", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.a or not args.b:
        ap.error("need two capture directories, or --self-test")

    problems = compare(args.a, args.b)
    for p in problems:
        print(f"DIFFERENCE {p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
