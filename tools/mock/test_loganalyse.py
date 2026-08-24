#!/usr/bin/env python3
"""Regression tests for tools/loganalyse.sh — TOOL-01, TOOL-02, TOOL-03.

Stdlib unittest only -- pytest is NOT available in this environment.

Expected values are derived from the specification (BACKLOG §2.3), never
from running the script under test.

Assertions are scoped to the relevant output *section* (not the whole
stdout), because tokens such as the payload string "H19" or the
"replaced_by_prio=1" field legitimately appear verbatim in other sections
(OVERVIEW echoes the last raw line; RING_OVERFLOW/DROPPED_PACKETS echo raw
drop lines). A whole-stdout match would give false failures.

Run with:
    python3 -m unittest discover -s tools/mock -p 'test_loganalyse.py'
    python3 tools/mock/test_loganalyse.py
"""

from __future__ import annotations

import re
import subprocess
import unittest
from pathlib import Path

REPO_ROOT: Path = Path(__file__).resolve().parents[2]
SCRIPT: Path = REPO_ROOT / "tools" / "loganalyse.sh"
FIXTURE: Path = REPO_ROOT / "tools" / "testdata" / "loganalyse_sm_sample.txt"


class TestLoganalyse(unittest.TestCase):
    """Shell-out tests for tools/loganalyse.sh against the serial-monitor
    sample fixture. The script is invoked once via setUpClass() to avoid
    redundant subprocess spawns.

    Fixture (loganalyse_sm_sample.txt) contains, by construction:
      - 4 benign rc=0 MC-SM transitions
      - 2 CSMA-backoff lines:  [MC-SM] TX_PREPARE -> IDLE rc=-1
      - 1 genuine fault:       [MC-SM] TX_ACTIVE -> TX_DONE rc=-2
      - 1 [MC-STAT] line (unlocks the PRIORITY_DISTRIBUTION drop breakdown)
      - 1 RING_DROP_PRIO  prio=5 type=40 replaced_by_prio=1
      - 1 RING_DROP_NEW   prio=5 type=40
      - 1 MH-LoRa line with real hop H02 and payload telemetry token H19
    """

    _stdout: str = ""
    _stderr: str = ""

    @classmethod
    def setUpClass(cls) -> None:
        result: subprocess.CompletedProcess[str] = subprocess.run(
            ["bash", str(SCRIPT), str(FIXTURE)],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        cls._stdout = result.stdout
        cls._stderr = result.stderr

    def _slice(self, start_marker: str, stop_prefixes: tuple[str, ...]) -> str:
        """Return the output block beginning at the line containing
        start_marker, up to (but excluding) the next line whose start
        matches any of stop_prefixes."""
        out: list[str] = []
        capturing = False
        for line in self._stdout.splitlines():
            if not capturing:
                if start_marker in line:
                    capturing = True
                continue
            if any(line.startswith(p) for p in stop_prefixes):
                break
            out.append(line)
        return "\n".join(out)

    # ------------------------------------------------------------------
    # TOOL-01: rc=-1 is CSMA back-off; rc=-2 is a genuine SM error.
    # The corrected script splits them into separate counters.
    # Fixture: 2x rc=-1, 1x rc=-2  =>  MC_SM_CSMA_BACKOFF: 2, MC_SM_ERRORS: 1
    # (MC_SM_ERRORS / MC_SM_CSMA_BACKOFF are unique tokens -> whole-stdout ok.)
    # ------------------------------------------------------------------

    def test_tool01_csma_backoff_split(self) -> None:
        stdout = self._stdout
        self.assertIn(
            "MC_SM_ERRORS: 1",
            stdout,
            msg=f"Expected 'MC_SM_ERRORS: 1' (only the rc=-2 fault).\nstdout:\n{stdout}",
        )
        self.assertIn(
            "MC_SM_CSMA_BACKOFF: 2",
            stdout,
            msg=f"Expected 'MC_SM_CSMA_BACKOFF: 2' (the two rc=-1 backoffs).\nstdout:\n{stdout}",
        )

    # ------------------------------------------------------------------
    # TOOL-02: drop breakdown groups RING_DROP_PRIO and RING_DROP_NEW by the
    # DROPPED packet's own prio+type. replaced_by_prio must NOT leak in.
    # Fixture: both drops prio=5 type=40 => combined count 2; replaced_by_prio=1
    # must not appear as a prio=1 breakdown entry.
    # Scoped to the "Dropped packets by prio+type" block only.
    # ------------------------------------------------------------------

    def test_tool02_drop_breakdown_by_prio_type(self) -> None:
        self.assertIn(
            "Dropped packets by prio+type",
            self._stdout,
            msg=f"Expected the 'Dropped packets by prio+type' block.\nstdout:\n{self._stdout}",
        )
        block = self._slice(
            "--- Dropped packets by prio+type ---", ("--- ", "=== ")
        )
        self.assertRegex(
            block,
            r"(?m)^\s*2\s+prio=5 type=40\s*$",
            msg=(
                "Expected 'prio=5 type=40' counted 2 (RING_DROP_PRIO + "
                f"RING_DROP_NEW) in the breakdown block.\nblock:\n{block}"
            ),
        )
        self.assertNotIn(
            "prio=1",
            block,
            msg=(
                "replaced_by_prio=1 leaked into the drop breakdown "
                f"(double-count bug).\nblock:\n{block}"
            ),
        )

    # ------------------------------------------------------------------
    # TOOL-03: hop extracted from the positional field, not payload tokens.
    # Fixture MH-LoRa line: real hop H02, payload telemetry token H19.
    # Scoped to the HOP_DISTRIBUTION section (H19 also appears in OVERVIEW's
    # echoed raw last line, which must not count).
    # ------------------------------------------------------------------

    def test_tool03_hop_excludes_payload(self) -> None:
        hop = self._slice("=== HOP_DISTRIBUTION ===", ("=== ",))
        self.assertIn(
            "H02",
            hop,
            msg=f"Expected hop 'H02' in HOP_DISTRIBUTION.\nsection:\n{hop}",
        )
        self.assertNotIn(
            "H19",
            hop,
            msg=(
                "H19 is a payload telemetry token, not a hop; it must not "
                f"appear in HOP_DISTRIBUTION.\nsection:\n{hop}"
            ),
        )


if __name__ == "__main__":
    unittest.main()
