#!/usr/bin/env python3
"""Per-env verdict from a `pio test --json-output-path` report, for tools/regression.sh.

    python3 tools/regression_unity.py <report.json> <env> [<env> ...]

Prints one tab-separated line per requested env, in the order given:

    <env> <OK|verdict> <cases> <ok> <failed> <skipped> <seconds>

An env is OK only when every suite pio ran for it passed and at least one case
ran. An env that is missing from the report, ran no case, or holds a failed or
errored suite gets its reason as the verdict. pio lists every suite once per
env of the project and marks the ones outside the env's test_filter SKIPPED;
those rows are not part of the env.
"""

from __future__ import annotations

import json
import sys


def env_rows(report: dict, envs: list[str]) -> list[tuple[str, str, int, int, int, int, int]]:
    """Aggregate the suites of *report* per env and judge each env of *envs*."""
    per_env: dict[str, dict[str, float]] = {}
    for suite in report.get("test_suites", []):
        if suite.get("status") == "SKIPPED" and not suite.get("testcase_nums"):
            continue
        agg = per_env.setdefault(
            suite["env_name"],
            {"suites": 0, "bad": 0, "cases": 0, "fail": 0, "skip": 0, "secs": 0.0},
        )
        agg["suites"] += 1
        agg["cases"] += suite.get("testcase_nums", 0)
        agg["fail"] += suite.get("failure_nums", 0) + suite.get("error_nums", 0)
        agg["skip"] += suite.get("skipped_nums", 0)
        agg["secs"] += suite.get("duration") or 0.0
        if suite.get("status") != "PASSED":
            agg["bad"] += 1
    rows = []
    for env in envs:
        agg = per_env.get(env)
        if agg is None:
            rows.append((env, "not in the report (did not build or run)", 0, 0, 0, 0, 0))
            continue
        cases, fail, skip = int(agg["cases"]), int(agg["fail"]), int(agg["skip"])
        if agg["bad"] or fail:
            verdict = f"{int(agg['bad'])} suite(s) not passed"
        elif cases == 0:
            verdict = "no test case ran"
        else:
            verdict = "OK"
        rows.append((env, verdict, cases, cases - fail - skip, fail, skip, round(agg["secs"])))
    return rows


def self_test() -> int:
    """Negative and positive cases on a hand-built report."""

    def suite(env: str, status: str, cases: int, fail: int = 0, skip: int = 0, err: int = 0) -> dict:
        return {
            "env_name": env, "status": status, "testcase_nums": cases, "failure_nums": fail,
            "error_nums": err, "skipped_nums": skip, "duration": 1.4,
        }

    report = {
        "test_suites": [
            suite("good", "PASSED", 5),
            suite("good", "PASSED", 3, skip=1),
            suite("good", "SKIPPED", 0),  # a suite outside the env's test_filter
            suite("red", "PASSED", 4),
            suite("red", "FAILED", 2, fail=1),
            suite("broken", "ERRORED", 0, err=1),
            suite("empty", "PASSED", 0),
            suite("other", "SKIPPED", 0),
        ]
    }
    rows = {r[0]: r for r in env_rows(report, ["good", "red", "broken", "empty", "other", "absent"])}
    checks = [
        (rows["good"][1:6] == ("OK", 8, 7, 0, 1), "two passed suites sum up, the filtered one is ignored"),
        (rows["good"][6] == 3, "durations of the env's suites are summed"),
        (rows["red"][1] != "OK" and rows["red"][4] == 1, "a failed suite makes the env red"),
        (rows["broken"][1] != "OK", "a build error makes the env red"),
        (rows["empty"][1] == "no test case ran", "zero cases is not a pass"),
        (rows["other"][1].startswith("not in the report"), "an env with only filtered suites is red"),
        (rows["absent"][1].startswith("not in the report"), "an env missing from the report is red"),
    ]
    for ok, what in checks:
        print(("PASS  " if ok else "FAIL  ") + what)
    return 0 if all(ok for ok, _ in checks) else 1


def main(argv: list[str]) -> int:
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8") as fp:
        report = json.load(fp)
    for row in env_rows(report, argv[2:]):
        print("\t".join(str(x) for x in row))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
