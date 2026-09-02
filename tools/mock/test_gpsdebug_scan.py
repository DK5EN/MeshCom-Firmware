#!/usr/bin/env python3
"""Regression tests for tools/bench/gpsdebug_scan.py -- the `--gpsdebug on` scan tool.

Stdlib unittest only -- pytest is NOT available in this environment.

Run with:
    python3 -m unittest discover -s tools/mock -p 'test_gpsdebug_scan.py'
    python3 tools/mock/test_gpsdebug_scan.py

Fixture specification
----------------------
tools/testdata/gpsdebug_sample.txt is two verbatim excerpts of a real
`--gpsdebug on` capture (~/Downloads/gpsdebug.txt, DK5EN-14, 2026-09-01),
joined by a comment line the scanner must ignore without failing:

  - lines 1-200 of the source: the cold-start phase (13x `fix:no sat:0`,
    a run of `fix:no sat:4/5`) followed by the first fix and 26 clean
    `fix:yes` evaluations with position + altitude.
  - source lines 655-685: the ONE corrupt evaluation in the whole 2047-line
    capture, plus 5 clean cycles on either side of it. The corrupt cycle's
    `Date:` line reads `2015.14.00` (month 14 -- calendar-impossible) and its
    `position` line reads `lon:0.000000` -- one spliced NMEA sentence, so
    the two symptoms are the SAME evaluation, not two.

Hand-counted (independently of gpsdebug_scan.py, straight from `grep -c`):

    evaluations (fix: lines)      55   (25 fix:no, 30 fix:yes)
    position lines                30   (== fix:yes count; a fix:no cycle
                                         never gets one in this firmware)
    date lines                    55   (== evaluations)
    corrupt position (lon==0)      1   (line 218)
    corrupt date (impossible)      1   (line 216, "2015.14.00")
    corrupt samples (deduplicated) 1   (lines 216 and 218 are one cycle)
    first fix:yes                  the 26th evaluation -> eval_index 25
                                    -> 25 * 3s = 75.0s (index-based cadence,
                                    the fixture carries no host timestamps)

Altitude, hand-computed with an independent script from the 30 `alt:`
values, samples at/after the first fix, excluding the one corrupt cycle
(29 values kept):

    median 279.4   rms-around-median 4.4870 (rounded 4.487)
    min 269.3   max 284.8
"""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT: Path = Path(__file__).resolve().parents[2]
SCRIPT: Path = REPO_ROOT / "tools" / "bench" / "gpsdebug_scan.py"
FIXTURE: Path = REPO_ROOT / "tools" / "testdata" / "gpsdebug_sample.txt"

sys.path.insert(0, str(REPO_ROOT / "tools" / "bench"))
import gpsdebug_scan  # noqa: E402  (path is set up above)


def _parse_fixture() -> gpsdebug_scan.ScanResult:
    with FIXTURE.open("r", encoding="utf-8") as fh:
        lines = fh.readlines()
    return gpsdebug_scan.parse_capture(lines)


class TestGpsdebugScan(unittest.TestCase):
    """In-process tests against the real-log fixture, plus synthetic edge cases
    and one CLI/--json round trip."""

    res: gpsdebug_scan.ScanResult

    @classmethod
    def setUpClass(cls) -> None:
        cls.res = _parse_fixture()

    # ------------------------------------------------------------------
    # Counts against the fixture spec above
    # ------------------------------------------------------------------

    def test_evaluation_and_fix_counts(self) -> None:
        self.assertEqual(self.res.evaluations, 55)
        self.assertEqual(self.res.fixes, 30)

    def test_position_and_date_line_counts(self) -> None:
        self.assertEqual(self.res.position_samples, 30)
        self.assertEqual(self.res.date_lines, 55)

    def test_capture_month_autodetected(self) -> None:
        self.assertEqual(self.res.capture_month, (2026, 9))

    def test_first_fix_time(self) -> None:
        self.assertEqual(self.res.first_fix_eval_index, 25)
        self.assertIsNotNone(self.res.first_fix_elapsed_s)
        assert self.res.first_fix_elapsed_s is not None
        self.assertAlmostEqual(self.res.first_fix_elapsed_s, 75.0, places=6)

    def test_unknown_gps_lines_do_not_break_parsing(self) -> None:
        # The fixture's joining comment line ("# --- gap: ... ---") is neither
        # a [GPS ] line nor blank; it must be silently ignored.
        with FIXTURE.open("r", encoding="utf-8") as fh:
            raw = fh.read()
        self.assertIn("# --- gap:", raw, msg="fixture must still carry its join marker")

    # ------------------------------------------------------------------
    # Corrupt-sample detection: exactly one lon==0.0 sample, the impossible
    # date is flagged, and the two symptoms dedup to ONE corrupt sample.
    # ------------------------------------------------------------------

    def test_exactly_one_lon_zero_sample(self) -> None:
        self.assertEqual(self.res.corrupt_position, 1)
        pos_examples = [e for e in self.res.corrupt_examples if e["kind"] == "position"]
        self.assertEqual(len(pos_examples), 1)
        self.assertEqual(pos_examples[0]["lon"], 0.0)
        self.assertEqual(pos_examples[0]["lat"], 48.247871)

    def test_impossible_date_is_flagged(self) -> None:
        self.assertEqual(self.res.corrupt_date, 1)
        date_examples = [e for e in self.res.corrupt_examples if e["kind"] == "date"]
        self.assertEqual(len(date_examples), 1)
        self.assertEqual(date_examples[0]["date"], "2015.14.00")

    def test_corrupt_samples_deduplicate_same_cycle(self) -> None:
        # The bad date and the lon==0 position are the SAME evaluation cycle
        # (one spliced sentence): the headline count must not double it.
        self.assertEqual(self.res.corrupt_samples, 1)

    def test_month_mismatch_is_also_corrupt(self) -> None:
        lines = [
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:00 / Date: 2026.09.01\n",
            "[GPS ]...position  : lat:48.100000 lon:14.100000 alt:250.0\n",
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:03 / Date: 2026.08.01\n",  # valid calendar, wrong month
            "[GPS ]...position  : lat:48.100001 lon:14.100001 alt:251.0\n",
        ]
        res = gpsdebug_scan.parse_capture(lines, month=(2026, 9))
        self.assertEqual(res.corrupt_date, 1)
        self.assertEqual(res.corrupt_samples, 1)

    def test_capture_month_vote_ignores_impossible_dates(self) -> None:
        # A corrupt date must not be allowed to vote for the reference month.
        # Here the vote is 2 corrupt (2015.14) against 2 good (2026.09) -- a tie
        # that Counter.most_common resolves by insertion order, i.e. in favour
        # of the corrupt month, unless impossible dates are filtered out first.
        # If they were, every good line of the capture would be reported as
        # wrong-month and the two genuinely corrupt cycles as clean.
        def cycle(date: str, lat: str) -> list[str]:
            return [
                "[GPS ]...fix:yes sat:6 hdop:2.0\n",
                f"[GPS ]...Time <UTC>: 12:00:00 / Date: {date}\n",
                f"[GPS ]...position  : lat:{lat} lon:14.100000 alt:250.0\n",
            ]

        lines = (
            cycle("2015.14.00", "48.100000")
            + cycle("2015.14.00", "48.100001")
            + cycle("2026.09.01", "48.100002")
            + cycle("2026.09.02", "48.100003")
        )

        res = gpsdebug_scan.parse_capture(lines)

        self.assertEqual(res.capture_month, (2026, 9))
        self.assertEqual(res.corrupt_date, 2, msg="only the two impossible dates are corrupt")
        self.assertEqual(res.corrupt_samples, 2)
        self.assertEqual(res.altitude_overall["n"], 2, msg="the two clean cycles keep their altitude")

    # ------------------------------------------------------------------
    # Altitude statistics, hand-computed independently (see module docstring)
    # ------------------------------------------------------------------

    def test_altitude_stats_after_first_fix(self) -> None:
        overall = self.res.altitude_overall
        self.assertEqual(overall["n"], 29, msg="30 positions minus the 1 corrupt cycle")
        self.assertAlmostEqual(overall["median"], 279.4, places=2)
        self.assertAlmostEqual(overall["rms_around_median"], 4.487, places=2)
        self.assertAlmostEqual(overall["min"], 269.3, places=2)
        self.assertAlmostEqual(overall["max"], 284.8, places=2)

    def test_altitude_buckets_cover_the_same_samples(self) -> None:
        total = sum(b["n"] for b in self.res.altitude_buckets)
        self.assertEqual(total, self.res.altitude_overall["n"])

    def test_index_based_bucketing_is_reported(self) -> None:
        # The fixture is a raw serial_session.py-style dump: no per-line host
        # timestamp, so the tool must fall back to (and say so) index-based
        # bucketing at the nominal 3s/evaluation cadence.
        self.assertFalse(self.res.timestamp_mode)
        d = gpsdebug_scan.result_to_dict(self.res)
        self.assertIn("sample-index", d["bucketing"])

    # ------------------------------------------------------------------
    # Synthetic edge cases: reject: lines, alt converged:, and identical
    # parsing with vs. without a host timestamp prefix.
    # ------------------------------------------------------------------

    def test_reject_lines_are_counted(self) -> None:
        lines = [
            "[GPS ]...fix:no sat:0 hdop:25.5\n",
            "[GPS ]...reject: lat:0.000000 lon:0.000000 date:2015.14.00 n:1\n",
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:00 / Date: 2026.09.01\n",
            "[GPS ]...position  : lat:48.100000 lon:14.100000 alt:250.0\n",
        ]
        res = gpsdebug_scan.parse_capture(lines)
        self.assertEqual(res.reject_lines, 1)
        # A reject: line does not itself carry a position, so it must not be
        # mistaken for a corrupt position sample.
        self.assertEqual(res.corrupt_position, 0)

    def test_alt_converged_first_occurrence(self) -> None:
        lines = [
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:00 / Date: 2026.09.01\n",
            "[GPS ]...position  : lat:48.100000 lon:14.100000 alt:250.0\n",
            "[GPS ]...alt converged: 251 m (P=2.3)\n",
            "[GPS ]...alt converged: 999 m (P=0.1)\n",  # a second one must not overwrite the first
        ]
        res = gpsdebug_scan.parse_capture(lines)
        self.assertIsNotNone(res.alt_converged_first)
        assert res.alt_converged_first is not None
        self.assertEqual(res.alt_converged_first["alt_m"], 251)

    def test_unrecognized_gps_line_is_ignored(self) -> None:
        lines = [
            "[GPS ]...NMEA: $GPGGA,fake*00\n",
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:00 / Date: 2026.09.01\n",
            "[GPS ]...position  : lat:48.100000 lon:14.100000 alt:250.0\n",
        ]
        res = gpsdebug_scan.parse_capture(lines)
        self.assertEqual(res.evaluations, 1)
        self.assertEqual(res.fixes, 1)

    def test_timestamp_prefix_parses_identically_to_none(self) -> None:
        bare = [
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:00 / Date: 2026.09.01\n",
            "[GPS ]...position  : lat:48.100000 lon:14.100000 alt:250.0\n",
            "[GPS ]...fix:yes sat:6 hdop:2.0\n",
            "[GPS ]...Time <UTC>: 12:00:03 / Date: 2026.09.01\n",
            "[GPS ]...position  : lat:48.100010 lon:14.100010 alt:252.0\n",
        ]
        prefixed = [f"2026-09-01 12:00:{i:02d}.000  {line}" for i, line in enumerate(bare)]

        res_bare = gpsdebug_scan.parse_capture(bare)
        res_prefixed = gpsdebug_scan.parse_capture(prefixed)

        self.assertFalse(res_bare.timestamp_mode)
        self.assertTrue(res_prefixed.timestamp_mode)
        self.assertEqual(res_bare.evaluations, res_prefixed.evaluations)
        self.assertEqual(res_bare.fixes, res_prefixed.fixes)
        self.assertEqual(res_bare.position_samples, res_prefixed.position_samples)
        self.assertEqual(res_bare.corrupt_samples, res_prefixed.corrupt_samples)
        self.assertEqual(res_bare.altitude_overall, res_prefixed.altitude_overall)

    # ------------------------------------------------------------------
    # CLI / --json round trip
    # ------------------------------------------------------------------

    def test_cli_json_round_trips(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(FIXTURE), "--json"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=f"CLI exited {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )
        data: dict[str, Any] = json.loads(result.stdout)
        self.assertEqual(data["evaluations"], 55)
        self.assertEqual(data["fixes"], 30)
        self.assertEqual(data["corrupt"]["samples"], 1)
        self.assertEqual(data["corrupt"]["position_lon_or_lat_zero"], 1)
        self.assertEqual(data["corrupt"]["date_impossible_or_wrong_month"], 1)
        self.assertAlmostEqual(data["altitude_after_first_fix"]["overall"]["median"], 279.4, places=2)

    def test_cli_text_output_does_not_crash(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(FIXTURE)],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        self.assertEqual(result.returncode, 0, msg=f"stderr:\n{result.stderr}")
        self.assertIn("corrupt samples: 1", result.stdout)


if __name__ == "__main__":
    unittest.main()
