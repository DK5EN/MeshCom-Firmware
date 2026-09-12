#!/usr/bin/env python3
"""Completeness gate for the Phase 2 HITL drift matrix (testplan §5.3).

`docs/testplan/drift-matrix.csv` is the one-row-per-observed-difference sign
-off record between the ESP32 and nRF52 implementations of each unified
function pair (testplan §5.1). It is filled in by hand during a review
session, not generated, so nothing stops a reviewer from leaving a row half
-done: an empty `verdict`, a `both-wrong` verdict with no `spec` sentence
saying what the correct behaviour actually is, or an `asserting_test` that
was typed from memory and never matches a real test. Any of those turns the
matrix from "the sign-off result that feeds the German PR description" (§5.3)
into a document that looks decided but isn't. This script is the gate the
testplan names `test_drift_matrix_complete`: it reads the CSV and fails
loudly on exactly those three holes, plus the structural ones (missing
header column, duplicate id, an enum value that isn't one of the ones the
testplan defines).

THE THREE PHASES, and why they exist in this order:

  --phase pre-review     verdicts may be empty (the M2 review has not happened)
  --phase implementation verdicts REQUIRED; asserting_test may be empty
                         (decided, but the waves have not written the tests yet)
  (default)              everything required -- the end state

Each is strictly stricter than the last, and each is TEMPORARY except the
default. `pre-review` became obsolete on 2026-09-12 when the M2 review filled
all 29 verdicts; `implementation` should be dropped the moment every row names
a test that exists. A phase flag left in place after its window has passed is
how a requirement quietly expires, so both print a warning saying so on every
run. Nothing downgrades an asserting_test NAME that does not resolve: that is
a typo or a rename in every phase, never a gap.

`asserting_test` resolution rule -- and why it is exact, not fuzzy:
    A CSV cell resolves to a real test only if it matches, character for
    character, either
      - `test/test_<name>/` as an existing directory, or
      - `[env:<name>]` as a section header in `platformio.ini`
    (a leading `env:` in the cell itself is stripped before that second
    check, so both `native_country_esp32` and `env:native_country_esp32`
    are accepted spellings of the same env). No substring, prefix or
    case-insensitive matching is done. A looser rule -- "contains a test
    name", "starts with test_" -- would let a typo or a stale name pass,
    which makes the whole check decorative: the one thing this column is
    for is "a name a human can search for and find code that runs when the
    decision is not implemented", and a rule that accepts near-misses
    breaks exactly that.

`--phase pre-review` is a TEMPORARY escape hatch, not a weaker gate. The
matrix is being populated right now with `verdict` deliberately empty on
every row -- a human fills it in during the §5.3 review session, so before
that session this check is *expected* to fail, and that failure is correct,
not a bug in the gate. `--phase pre-review` downgrades ONLY the empty
-verdict check, from a per-row violation to a reported count, so CI can run
this script while the matrix is still being drafted without either turning
the check off entirely or blocking unrelated work on a known, temporary
gap. Every other check -- both-wrong needs a spec, asserting_test must
resolve, enum values, duplicate ids, the header shape -- stays hard in both
phases. Once the review session has happened and every row has a verdict,
stop passing `--phase pre-review`: left in place, it would silently let a
row that lost its verdict again (a bad merge, a copy-paste) back to a count
instead of a failure.

  python3 test/golden/drift_matrix_lint.py                  # phase=default
  python3 test/golden/drift_matrix_lint.py --phase pre-review
  python3 test/golden/drift_matrix_lint.py --self-test

Exit 0 when the matrix is clean for the given phase, 1 on any violation.
Exit 1 (fatal, never a silent clean pass) when the CSV file is missing, is
empty or does not parse as CSV with a header row, or the header is missing
a required column -- a broken instrument must never look like a pass.
"""
import argparse
import csv
import io
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

REPO = Path(__file__).resolve().parents[2]
CSV_SOURCE = "docs/testplan/drift-matrix.csv"
PLATFORMIO_INI = "platformio.ini"

# testplan §5.1 -- exact column set the CSV header must carry.
REQUIRED_COLUMNS = [
    "id", "uut", "pair", "esp32_behaviour", "nrf52_behaviour", "evidence",
    "class", "tie_break", "recommendation", "verdict", "spec",
    "decided_by", "decided_on", "after_expect", "asserting_test",
]

VALID_AFTER_EXPECT = {"identical", "nrf52-changes", "esp32-changes", "both-change"}
VALID_CLASS = {"bug-one-side", "platform-api", "feature-one-side", "cosmetic"}

CASE_DEF = re.compile(r"^\s*(?:static\s+)?void\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", re.M)
RUN_TEST = re.compile(r"RUN_TEST\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
ENV_HEADER = re.compile(r'^\[env:([^\]]+)\]\s*$', re.M)


@dataclass
class AnalysisResult:
    row_count: int = 0
    violations: List[str] = field(default_factory=list)
    empty_verdict_count: int = 0
    missing_test_count: int = 0
    fatal: List[str] = field(default_factory=list)


def resolve_test_names(repo: Path) -> Tuple[Set[str], Set[str]]:
    """Real-tree lookup tables for asserting_test resolution.

    Returns (test_dir_names, platformio_env_names) -- both exact-name sets,
    read fresh from disk so this always reflects the current tree rather
    than a snapshot that can drift from it.
    """
    test_root = repo / "test"
    test_dirs = {p.name for p in test_root.iterdir() if p.is_dir()} if test_root.is_dir() else set()
    ini_path = repo / PLATFORMIO_INI
    ini_text = ini_path.read_text() if ini_path.exists() else ""
    envs = set(ENV_HEADER.findall(ini_text))
    return test_dirs, envs


def build_case_index(repo: Path, test_dirs: Set[str]) -> Dict[str, Set[str]]:
    """Identifiers defined in each test suite's sources, for `suite::case`.

    Deliberately crude: every `void name(` / `RUN_TEST(name)` token in the
    suite's .cpp/.c/.h files. A crude index errs towards accepting a real
    case, never towards inventing one, because a name that appears nowhere
    in the suite cannot be matched by it.
    """
    index: Dict[str, Set[str]] = {}
    for d in sorted(test_dirs):
        names: Set[str] = set()
        p = repo / "test" / d
        if not p.is_dir():
            continue
        for f in sorted(p.rglob("*")):
            if f.suffix not in (".cpp", ".c", ".h", ".hpp"):
                continue
            try:
                text = f.read_text(errors="replace")
            except OSError:
                continue
            names.update(CASE_DEF.findall(text))
            names.update(RUN_TEST.findall(text))
        index[d] = names
    return index


def asserting_test_exists(name: str, test_dirs: Set[str], envs: Set[str],
                          case_index: Optional[Dict[str, Set[str]]] = None) -> bool:
    """Exact-match resolution only -- see the module docstring for why.

    Two spellings are accepted:

      `suite`            -- a test/test_<suite>/ directory or a [env:<suite>]
      `suite::case`      -- the same, plus the case name must actually occur
                            as an identifier in that suite's sources

    A cell may list several of these separated by `;`, for a difference that
    more than one case pins. EVERY part must resolve: "at least one resolves"
    would let a stale name ride along beside a live one, which is the failure
    this check exists to catch.

    The `suite::case` form is the one the drift matrix uses, and it is the
    reason this check is worth running at all. Resolving only the suite would
    let a row keep pointing at a case that has since been renamed or deleted:
    the suite still exists, the gate still says clean, and the row's claim
    that something is asserted is no longer true. `case_index` maps a suite
    directory name to the set of identifiers defined in its sources; when it
    is None the case half is not checked (used by the isolated self-test
    cases, which have no tree to index).
    """
    name = name.strip()
    if not name:
        return False
    suite, _, case = name.partition("::")
    suite = suite.strip()
    case = case.strip()
    if not suite:
        return False

    candidate_dir = suite if suite.startswith("test_") else f"test_{suite}"
    if candidate_dir in test_dirs:
        if case and case_index is not None:
            return case in case_index.get(candidate_dir, set())
        return True

    if case:
        # a PlatformIO env is not a thing that can contain a named case
        return False
    env_name = suite[len("env:"):] if suite.startswith("env:") else suite
    return env_name in envs


def analyze(csv_text: str, test_dirs: Set[str], envs: Set[str], phase: str,
            case_index: Optional[Dict[str, Set[str]]] = None) -> AnalysisResult:
    result = AnalysisResult()

    reader = csv.DictReader(io.StringIO(csv_text))
    fieldnames = reader.fieldnames
    if not fieldnames:
        result.fatal.append(
            "no header row found -- the file is empty or does not parse as "
            "CSV at all; this check would otherwise report a false clean pass")
        return result

    missing_columns = [c for c in REQUIRED_COLUMNS if c not in fieldnames]
    if missing_columns:
        result.fatal.append(
            "header is missing required column(s): " + ", ".join(missing_columns) +
            f" (testplan §5.1 requires: {', '.join(REQUIRED_COLUMNS)}; "
            f"found: {', '.join(fieldnames)})")
        return result

    rows = list(reader)
    if not rows:
        result.fatal.append(
            "header parsed but zero data rows found -- nothing to check; a "
            "matrix with no rows is not a clean matrix, it is an empty one")
        return result

    result.row_count = len(rows)
    seen_ids: Dict[str, int] = {}

    for i, row in enumerate(rows):
        line = i + 2  # header occupies line 1
        raw_id = (row.get("id") or "").strip()
        row_label = raw_id if raw_id else f"<row at line {line}, no id>"

        if raw_id:
            if raw_id in seen_ids:
                result.violations.append(
                    f"{row_label}: duplicate id, first seen at line "
                    f"{seen_ids[raw_id]}, again at line {line}")
            else:
                seen_ids[raw_id] = line

        verdict = (row.get("verdict") or "").strip()
        if not verdict:
            result.empty_verdict_count += 1
            if phase != "pre-review":
                result.violations.append(f"{row_label}: empty verdict")

        if verdict == "both-wrong":
            spec = (row.get("spec") or "").strip()
            if not spec:
                result.violations.append(
                    f"{row_label}: verdict is 'both-wrong' but spec is empty "
                    f"-- state the one correct behaviour both sides should "
                    f"have")

        asserting_test = (row.get("asserting_test") or "").strip()
        if not asserting_test:
            # An empty cell is an honest "no test asserts this yet". Before the
            # review session that is a gap to count; after it, every row must
            # name the test that fails if the decision is not implemented, so
            # it becomes a violation. A row that instead spells the gap out in
            # prose ("none yet") is worse than empty: it reads like a name.
            result.missing_test_count += 1
            if phase not in ("pre-review", "implementation"):
                result.violations.append(
                    f"{row_label}: asserting_test is empty -- name the test "
                    f"that will fail if this decision is not implemented")
        elif not all(asserting_test_exists(part, test_dirs, envs, case_index)
                     for part in asserting_test.split(";") if part.strip()):
            # A NAME that does not resolve is always a violation, in every
            # phase: it is a typo or a rename, not a gap, and silently
            # tolerating it is what makes this column decorative.
            result.violations.append(
                f"{row_label}: asserting_test {asserting_test!r} does not "
                f"resolve -- expected 'suite' or 'suite::case' where suite is "
                f"a test/test_<name>/ directory or a platformio.ini "
                f"[env:<name>], and case is an identifier in that suite")

        after_expect = (row.get("after_expect") or "").strip()
        if after_expect and after_expect not in VALID_AFTER_EXPECT:
            result.violations.append(
                f"{row_label}: after_expect {after_expect!r} is not one of "
                f"{sorted(VALID_AFTER_EXPECT)}")

        klass = (row.get("class") or "").strip()
        if klass and klass not in VALID_CLASS:
            result.violations.append(
                f"{row_label}: class {klass!r} is not one of "
                f"{sorted(VALID_CLASS)}")

    return result


def check(path: Optional[Path] = None, phase: str = "default") -> AnalysisResult:
    """Run the analysis against a real file. A missing file is fatal, not an
    empty/clean result -- see the module docstring."""
    p = path if path is not None else (REPO / CSV_SOURCE)
    if not p.exists():
        r = AnalysisResult()
        r.fatal.append(
            f"{p} does not exist -- the drift matrix has not been written "
            f"(yet), or was moved; this is not a clean pass")
        return r
    text = p.read_text()
    test_dirs, envs = resolve_test_names(REPO)
    case_index = build_case_index(REPO, test_dirs)
    return analyze(text, test_dirs, envs, phase, case_index)


def _row(values: Dict[str, str]) -> str:
    return ",".join(values.get(c, "") for c in REQUIRED_COLUMNS)


def self_test() -> int:
    ok = True
    header = ",".join(REQUIRED_COLUMNS)
    fake_dirs = {"test_zero_scan"}
    fake_envs = {"native_country_esp32"}

    base_row = {
        "id": "DR-01", "uut": "U1", "pair": "zero_scan",
        "esp32_behaviour": "reads in-range", "nrf52_behaviour": "reads buf[i+1]",
        "evidence": "corpus/u1/07.bin:12", "class": "bug-one-side",
        "tie_break": "", "recommendation": "adopt ESP32 bound check",
        "verdict": "esp32-correct", "spec": "",
        "decided_by": "dk5en", "decided_on": "2026-09-12",
        "after_expect": "nrf52-changes", "asserting_test": "zero_scan",
    }

    def merged(**overrides: str) -> Dict[str, str]:
        r = dict(base_row)
        r.update(overrides)
        return r

    def csv_of(*rows: Dict[str, str]) -> str:
        return "\n".join([header] + [_row(r) for r in rows]) + "\n"

    # --- case 1: clean matrix, both asserting_test resolution paths ---
    clean_csv = csv_of(
        merged(),
        merged(id="DR-02", uut="U6", pair="country_table",
               **{"class": "platform-api"}, verdict="both-valid",
               after_expect="identical", asserting_test="native_country_esp32"),
    )
    r = analyze(clean_csv, fake_dirs, fake_envs, "default")
    good = not r.fatal and not r.violations and r.row_count == 2
    print(f"  {'ok ' if good else 'FAIL'} clean matrix: "
          f"{r.row_count} row(s), {len(r.violations)} violation(s), "
          f"fatal={r.fatal}")
    ok = ok and good

    # --- case 2: empty verdict, default phase -> hard violation ---
    empty_verdict_csv = csv_of(merged(verdict=""))
    r = analyze(empty_verdict_csv, fake_dirs, fake_envs, "default")
    good = (not r.fatal and r.empty_verdict_count == 1
            and any("empty verdict" in v for v in r.violations))
    print(f"  {'ok ' if good else 'FAIL'} empty verdict (default phase): "
          f"{len(r.violations)} violation(s)")
    ok = ok and good

    # --- case 2b: same row, pre-review phase -> downgraded to a count ---
    r = analyze(empty_verdict_csv, fake_dirs, fake_envs, "pre-review")
    good = (not r.fatal and r.empty_verdict_count == 1
            and not any("empty verdict" in v for v in r.violations))
    print(f"  {'ok ' if good else 'FAIL'} empty verdict (pre-review phase, "
          f"downgraded): empty_verdict_count={r.empty_verdict_count}, "
          f"violations={len(r.violations)}")
    ok = ok and good

    # --- case 3: both-wrong with no spec -> hard violation in both phases ---
    both_wrong_csv = csv_of(merged(verdict="both-wrong", spec=""))
    for phase in ("default", "pre-review"):
        r = analyze(both_wrong_csv, fake_dirs, fake_envs, phase)
        good = not r.fatal and any("spec is empty" in v for v in r.violations)
        print(f"  {'ok ' if good else 'FAIL'} both-wrong without spec "
              f"(phase={phase}): {r.violations}")
        ok = ok and good

    # --- case 4: asserting_test does not resolve ---
    bad_test_csv = csv_of(merged(asserting_test="totally_made_up_name"))
    r = analyze(bad_test_csv, fake_dirs, fake_envs, "default")
    good = not r.fatal and any("does not resolve" in v for v in r.violations)
    print(f"  {'ok ' if good else 'FAIL'} asserting_test does not exist: "
          f"{r.violations}")
    ok = ok and good

    # --- case 5: duplicate id ---
    dup_csv = csv_of(merged(id="DR-09"), merged(id="DR-09", pair="other"))
    r = analyze(dup_csv, fake_dirs, fake_envs, "default")
    good = not r.fatal and any("duplicate id" in v for v in r.violations)
    print(f"  {'ok ' if good else 'FAIL'} duplicate id: {r.violations}")
    ok = ok and good

    # --- case 6: bad after_expect value ---
    bad_after_csv = csv_of(merged(after_expect="sideways"))
    r = analyze(bad_after_csv, fake_dirs, fake_envs, "default")
    good = not r.fatal and any("after_expect" in v for v in r.violations)
    print(f"  {'ok ' if good else 'FAIL'} bad after_expect value: {r.violations}")
    ok = ok and good

    # --- case 6b: bad class value (same shape as after_expect, not in the
    # brief's minimum list but cheap to cover since the check is symmetric) ---
    bad_class_csv = csv_of(merged(**{"class": "not-a-real-class"}))
    r = analyze(bad_class_csv, fake_dirs, fake_envs, "default")
    good = not r.fatal and any("class" in v and "not-a-real-class" in v for v in r.violations)
    print(f"  {'ok ' if good else 'FAIL'} bad class value: {r.violations}")
    ok = ok and good

    # --- case 7: missing required column in the header ---
    header_missing_spec = ",".join(c for c in REQUIRED_COLUMNS if c != "spec")
    missing_col_csv = header_missing_spec + "\n" + _row(merged()) + "\n"
    r = analyze(missing_col_csv, fake_dirs, fake_envs, "default")
    good = bool(r.fatal) and any("spec" in f for f in r.fatal) and not r.violations
    print(f"  {'ok ' if good else 'FAIL'} missing required column (fatal, "
          f"not a per-row violation): {r.fatal}")
    ok = ok and good

    # --- case 8: completely empty / unparseable file -> fatal, not clean ---
    r = analyze("", fake_dirs, fake_envs, "default")
    good = bool(r.fatal) and r.row_count == 0 and not r.violations
    print(f"  {'ok ' if good else 'FAIL'} empty file (fatal, not a clean "
          f"pass): {r.fatal}")
    ok = ok and good

    # --- case 9: header-only file (no data rows) -> also fatal ---
    r = analyze(header + "\n", fake_dirs, fake_envs, "default")
    good = bool(r.fatal) and "zero data rows" in r.fatal[0]
    print(f"  {'ok ' if good else 'FAIL'} header with zero data rows "
          f"(fatal): {r.fatal}")
    ok = ok and good

    # --- case 10: missing CSV file on disk -> fatal via check() ---
    r = check(path=REPO / "test" / "golden" / "does-not-exist-drift-matrix.csv")
    good = bool(r.fatal) and "does not exist" in r.fatal[0]
    print(f"  {'ok ' if good else 'FAIL'} missing file on disk (fatal via "
          f"check()): {r.fatal}")
    ok = ok and good

    # --- case: empty asserting_test is a gap pre-review, a violation after ---
    no_test_csv = csv_of(merged(asserting_test="", verdict="esp32-correct"))
    r = analyze(no_test_csv, fake_dirs, fake_envs, "default")
    good = (not r.fatal and r.missing_test_count == 1
            and any("asserting_test is empty" in v for v in r.violations))
    print(f"  {'ok ' if good else 'FAIL'} empty asserting_test (default "
          f"phase): {r.violations}")
    ok = ok and good

    r = analyze(no_test_csv, fake_dirs, fake_envs, "pre-review")
    good = (not r.fatal and r.missing_test_count == 1 and not r.violations)
    print(f"  {'ok ' if good else 'FAIL'} empty asserting_test (pre-review, "
          f"downgraded to a counted gap): missing_test_count="
          f"{r.missing_test_count}, violations={len(r.violations)}")
    ok = ok and good

    # A name that does not resolve stays a violation in EVERY phase -- that is
    # the difference between a gap and a typo, and the reason --phase
    # pre-review does not make this gate decorative.
    r = analyze(csv_of(merged(asserting_test="no_such_suite")),
                fake_dirs, fake_envs, "pre-review")
    good = (not r.fatal and any("does not resolve" in v for v in r.violations))
    print(f"  {'ok ' if good else 'FAIL'} unresolvable asserting_test is a "
          f"violation even pre-review: {r.violations}")
    ok = ok and good

    # --- the implementation phase: verdicts hard, missing tests counted ---
    # Guards the one way this phase could be wrong: it must NOT also let an
    # empty verdict through, or it would silently become pre-review.
    impl_no_test = csv_of(merged(asserting_test="", verdict="esp32-correct"))
    r = analyze(impl_no_test, fake_dirs, fake_envs, "implementation")
    good = (not r.fatal and r.missing_test_count == 1 and not r.violations)
    print(f"  {'ok ' if good else 'FAIL'} implementation phase downgrades an empty "
          f"asserting_test: missing_test_count={r.missing_test_count}, "
          f"violations={len(r.violations)}")
    ok = ok and good

    impl_no_verdict = csv_of(merged(verdict="", asserting_test="zero_scan"))
    r = analyze(impl_no_verdict, fake_dirs, fake_envs, "implementation")
    good = (not r.fatal and any("empty verdict" in v for v in r.violations))
    print(f"  {'ok ' if good else 'FAIL'} implementation phase still FAILS an empty "
          f"verdict (must not become pre-review): {r.violations}")
    ok = ok and good

    r = analyze(csv_of(merged(asserting_test="no_such_suite")),
                fake_dirs, fake_envs, "implementation")
    good = (not r.fatal and any("does not resolve" in v for v in r.violations))
    print(f"  {'ok ' if good else 'FAIL'} implementation phase still FAILS an "
          f"unresolvable asserting_test name: {r.violations}")
    ok = ok and good

    # --- asserting_test resolution rule itself, in isolation ---
    resolution_cases = [
        ("bare name matching a test dir", "zero_scan", True),
        ("name already carrying the test_ prefix", "test_zero_scan", True),
        ("bare name matching a platformio env", "native_country_esp32", True),
        ("env: prefixed spelling of the same env", "env:native_country_esp32", True),
        ("empty string", "", False),
        ("unrelated name", "softser_app", False),
        ("substring of a real dir name must NOT match", "zero_sca", False),
        # suite::case -- the spelling the drift matrix actually uses. With no
        # case_index supplied the case half is not checked; the indexed
        # behaviour is covered by the real-tree run, not here.
        ("suite::case resolves when the suite does", "zero_scan::test_x", True),
        ("suite::case with an unknown suite does not", "nope::test_x", False),
        ("a PlatformIO env cannot carry a ::case",
         "native_country_esp32::test_x", False),
    ]
    for name, value, want in resolution_cases:
        got = asserting_test_exists(value, fake_dirs, fake_envs)
        good = got == want
        print(f"  {'ok ' if good else 'FAIL'} asserting_test resolution -- "
              f"{name} ({value!r}): got={got} want={want}")
        ok = ok and good

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument(
        "--phase", choices=["default", "pre-review", "implementation"],
        default="default",
        help="'pre-review' downgrades the empty-verdict check to a reported "
             "count (still exits 0) while keeping every other check hard. "
             "TEMPORARY -- stop passing this once the §5.3 review session "
             "has happened and every row has a verdict.")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    result = check(phase=args.phase)
    for f in result.fatal:
        print(f"FATAL: {f}")
    if result.fatal:
        return 1

    print(f"{result.row_count} row(s) checked (phase={args.phase})")
    if args.phase == "implementation":
        print(
            f"phase=implementation: {result.missing_test_count} row(s) with no "
            f"asserting_test yet -- decided but not yet implemented, so the test "
            f"that would fail does not exist. TEMPORARY: drop this flag once the "
            f"unification waves have written them; verdicts are already enforced "
            f"hard in this phase")
    if args.phase == "pre-review":
        print(
            f"phase=pre-review: {result.empty_verdict_count} row(s) with "
            f"empty verdict, not counted as violations -- TEMPORARY, stop "
            f"passing --phase pre-review once the review session has "
            f"happened")
        print(
            f"phase=pre-review: {result.missing_test_count} row(s) with no "
            f"asserting_test yet -- a gap, reported so it cannot be mistaken "
            f"for coverage; a NAME that does not resolve is still a violation")

    for v in result.violations:
        print(f"VIOLATION {v}")

    if result.violations:
        print(f"\n{len(result.violations)} violation(s)")
        return 1

    print("drift matrix: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
