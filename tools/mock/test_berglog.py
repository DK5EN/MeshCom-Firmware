#!/usr/bin/env python3
"""Regression tests for tools/berglog.py -- the berg-node log analyser.

Stdlib unittest only -- pytest is NOT available in this environment.

Expected values are derived from the FIXTURE SPECIFICATION below (hand-counted
from the two synthetic logs), never from running the analyser under test.

The fixtures carry no real operator traffic: every station is a reserved-looking
OE0xxx-n callsign. The one exception is OE1XAR-33, which is not an operator but
the server time-signal injector hard-coded in berglog.TIME_SIGNAL_CALLS -- the
time-signal code path cannot be exercised without it.

Run with:
    python3 -m unittest discover -s tools/mock -p 'test_berglog.py'
    python3 tools/mock/test_berglog.py

Fixture specification
---------------------
berglog_sample_a.txt -- node OE0AAA-1, 26 lines, 16 of them `[LOG]` frames:

    msg_id    type  path                                           note
    11110001  :     OE0BBB-2                                       direct, shared with B
    11110001  :     OE0BBB-2,OE0CCC-3                              2nd copy
    11110001  :     OE0BBB-2,OE0AAA-1,OE0CCC-3                     own-relay echo 1
    11110002  !     OE0BBB-2,OE0CCC-3                              position, /N7
    11110002  !     OE0BBB-2,OE0AAA-1,OE0EEE-5                     own-relay echo 2
    11110003  @     OE0BBB-2                                       HEY direct, "R5;"
    11110003  @     OE0BBB-2,OE0AAA-1,OE0CCC-3                     own-relay echo 3, 2 reports
    11110004  @     OE0EEE-5,OE0FFF-6,OE0GGG-7,OE0HHH-8,OE0BBB-2   4 hops, 4 reports
    11110008  :     OE0MMM-13,OE0BBB-2,OE0CCC-3                    decoy: OE0BBB-2 at path[-2]
    11110008  :     OE0MMM-13,OE0BBB-2,OE0EEE-5                    decoy: OE0BBB-2 at path[-2]
    11110009  :     OE0III-9,OE0JJJ-10,OE0KKK-11                   H0F -> hop-counter wrap
    00000000  .     (none)                                         FCS:0000 decode failure
    1111000D  :     OE0BBB-                                        FCS:0000, NON-ZERO msg_id
    1111000A  :     OE1XAR-33,OE0BBB-2                             time signal, shared with B
    1111000B  :     OE0CCC-3                                       only heard by A
    1111000C  :     OE1XAR-33,OE0CCC-3                             2nd time signal, A only

    => 14 receptions, 2 decode failures, 9 unique msg_ids,
       types  : 9   @ 3   ! 2,
       direct neighbours OE0BBB-2, OE0CCC-3, OE0EEE-5, OE0KKK-11.

berglog_sample_b.txt -- node OE0DDD-4, 20 lines, 10 of them `[LOG]` frames:

    11110001  :     OE0BBB-2,OE0DDD-4,OE0CCC-3                     shared + own-relay echo 1
    11110001  :     OE0BBB-2,OE0EEE-5                              2nd copy
    11110005  :     OE0CCC-3                                       only heard by B
    11110005  :     OE0CCC-3,OE0DDD-4,OE0EEE-5                     own-relay echo 2
    11110006  !     OE0FFF-6,OE0CCC-3                              position, /N3
    11110007  @     OE0GGG-7                                       HEY direct, "R1;"
    11110007  @     OE0GGG-7,OE0DDD-4,OE0CCC-3                     own-relay echo 3, 2 reports
    11110009  :     OE0III-9,OE0JJJ-10,OE0EEE-5                    H0F -> hop-counter wrap
    00000000  .     (none)                                         FCS:0000 decode failure
    1111000A  :     OE1XAR-33,OE0CCC-3                             time signal, shared with A

    => 9 receptions, 1 decode failure, 6 unique msg_ids,
       types  : 6   @ 2   ! 1,
       direct neighbours OE0CCC-3, OE0EEE-5, OE0GGG-7.

The 1111000D frame is what pins the exclusion rule: decodeAPRS() bailed mid-path
(the callsign is truncated to "OE0BBB-", dest and payload are empty) but the msg_id
bytes had already been read, so ONLY FCS:0000 marks it as a decode failure. A rule
keyed on msg_id == 0 alone would let it through.

The two 11110008 decoys give OE0BBB-2 four appearances at path[-2] -- one MORE than
the own call OE0AAA-1. Raw penultimate frequency therefore points at the wrong
station, and only the "a node is never the LAST hop of a frame it received" guard
(OE0BBB-2 is the last hop four times, OE0AAA-1 never) recovers the right answer.

Both files open with a `[GW];keep;tx` keepalive followed by the `UDP Out Buff`
hex dump that spells the node's own callsign -- the independent ground truth the
positional own-call derivation is checked against.

    union of msg_ids   = 12   (A only 6, both 3, B only 3)
    hop-counter wraps  = OE0KKK-11 (A) and OE0EEE-5 (B), one frame each
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT: Path = Path(__file__).resolve().parents[2]
SCRIPT: Path = REPO_ROOT / "tools" / "berglog.py"
FIXTURE_A: Path = REPO_ROOT / "tools" / "testdata" / "berglog_sample_a.txt"
FIXTURE_B: Path = REPO_ROOT / "tools" / "testdata" / "berglog_sample_b.txt"

sys.path.insert(0, str(REPO_ROOT / "tools"))
import berglog  # noqa: E402  (path is set up above)

LABEL_A = "berglog_sample_a"
LABEL_B = "berglog_sample_b"


def own_call_from_keepalive(path: Path) -> str:
    """Decode the `UDP Out Buff` hex dump and return the callsign it spells.

    This is the fixture's independent ground truth: the gateway keepalive frame
    carries the node's configured callsign verbatim. berglog derives the own call
    positionally instead (a node never appears as the LAST hop of a frame it
    received), so agreeing with this decode is a real cross-check.
    """
    for line in path.read_text(encoding="utf-8").splitlines():
        marker = "UDP Out Buff:"
        if marker not in line:
            continue
        hex_part = line.split(marker, 1)[1].strip()
        raw = bytes(int(token, 16) for token in hex_part.split())
        text = raw.decode("ascii", "replace")
        start = text.index("OE0")
        return text[start : start + 8]
    raise AssertionError(f"no 'UDP Out Buff:' keepalive in {path}")


class TestBerglog(unittest.TestCase):
    """In-process tests against the two synthetic fixtures, plus one CLI run."""

    nodes: list[berglog.NodeLog] = []
    receivers: list[berglog.NodeLog] = []
    res: dict[str, Any] = {}

    @classmethod
    def setUpClass(cls) -> None:
        cls.nodes = berglog.parse_logs([FIXTURE_A, FIXTURE_B])
        cls.receivers = [n for n in cls.nodes if n.receptions]
        res: dict[str, Any] = {
            "01_overview": berglog.a01_overview(cls.nodes),
            "02_types": berglog.a02_types(cls.nodes),
            "03_hops": berglog.a03_hops(cls.nodes),
            "05_neighbours": berglog.a05_neighbours(cls.nodes),
            "06_redundancy": berglog.a06_redundancy(cls.nodes),
            "07_cross_node": berglog.a07_cross(cls.receivers),
            "09_time_signal": berglog.a09_time_signal(cls.nodes),
            "13_hey": berglog.a13_hey(cls.nodes),
            "16_own_echo": berglog.a16_own_echo(cls.nodes),
            "19_first_relayer": berglog.a19_first_relayer(cls.receivers),
            "20_relay_latency": berglog.a20_relay_latency(cls.receivers),
        }
        res["22_wrap_culprits"] = berglog.a22_wrap_culprits(cls.nodes, res)
        cls.res = res

    def _node(self, label: str) -> berglog.NodeLog:
        for n in self.nodes:
            if n.label == label:
                return n
        raise AssertionError(f"no parsed log labelled {label}")

    # ------------------------------------------------------------------
    # Identity: the positional derivation must land on the callsign the
    # gateway keepalive spells out. Two logs whose stems share a prefix must
    # still get distinct labels, or their statistics would silently merge.
    # ------------------------------------------------------------------

    def test_identity_matches_callsign_in_keepalive(self) -> None:
        node_a = self._node(LABEL_A)
        penultimate: dict[str, int] = {}
        for r in node_a.receptions:
            if len(r.path) >= 2:
                penultimate[r.path[-2]] = penultimate.get(r.path[-2], 0) + 1
        self.assertGreater(
            penultimate.get("OE0BBB-2", 0),
            penultimate.get("OE0AAA-1", 0),
            msg=(
                "The fixture must keep the decoy relayer ahead of the own call on raw "
                "path[-2] frequency, otherwise this test would pass without the "
                "never-the-last-hop guard doing any work."
            ),
        )
        for path, label in ((FIXTURE_A, LABEL_A), (FIXTURE_B, LABEL_B)):
            expected = own_call_from_keepalive(path)
            got = self._node(label).own_call
            self.assertEqual(
                got,
                expected,
                msg=(
                    f"{label}: positional own-call derivation returned {got!r}, but the "
                    f"keepalive in {path.name} spells {expected!r}."
                ),
            )

    def test_labels_stay_unique_for_shared_stem_prefix(self) -> None:
        labels = [n.label for n in self.nodes]
        self.assertEqual(
            labels,
            [LABEL_A, LABEL_B],
            msg=(
                "Both fixtures' stems start with 'berglog', so the short label would "
                f"collide and merge the two nodes. Got {labels}."
            ),
        )

    # ------------------------------------------------------------------
    # Reception counts and the FCS:0000 exclusion. decodeAPRS() leaves the
    # struct at its defaults when it bails, so those lines are counted as
    # frames but must not reach any message statistic.
    # ------------------------------------------------------------------

    def test_reception_counts(self) -> None:
        expected = {LABEL_A: (14, 2, 9), LABEL_B: (9, 1, 6)}
        for label, (rx, undecodable, uniq) in expected.items():
            node = self._node(label)
            self.assertEqual(len(node.receptions), rx, msg=f"{label}: receptions")
            self.assertEqual(len(node.undecodable), undecodable, msg=f"{label}: decode failures")
            self.assertEqual(
                len({r.msg_id for r in node.receptions}), uniq, msg=f"{label}: unique msg_ids"
            )

    def test_fcs0000_lines_are_excluded_from_messages(self) -> None:
        for label in (LABEL_A, LABEL_B):
            node = self._node(label)
            self.assertTrue(
                all(u["raw"].count("FCS:0000") == 1 for u in node.undecodable),
                msg=f"{label}: every decode failure must be an FCS:0000 line",
            )
            self.assertNotIn(
                "00000000",
                {r.msg_id for r in node.receptions},
                msg=f"{label}: the FCS:0000 frame leaked into the receptions",
            )
            self.assertEqual(
                len(node.receptions) + len(node.undecodable),
                self.res["02_types"][label]["total_log_lines"],
                msg=f"{label}: [LOG] frame lines must split into receptions + decode failures",
            )
        self.assertNotIn(
            "1111000D",
            {r.msg_id for r in self._node(LABEL_A).receptions},
            msg=(
                "A truncated frame with FCS:0000 but a NON-ZERO msg_id must still be "
                "excluded -- FCS is the decode-failure marker, msg_id alone is not."
            ),
        )

    def test_message_type_split(self) -> None:
        expected = {LABEL_A: {":": 9, "@": 3, "!": 2}, LABEL_B: {":": 6, "@": 2, "!": 1}}
        for label, per_type in expected.items():
            node = self._node(label)
            got = {t: sum(1 for r in node.receptions if r.ptype == t) for t in ":@!"}
            self.assertEqual(got, per_type, msg=f"{label}: receptions per payload type")

    def test_four_hop_path_is_parsed(self) -> None:
        node = self._node(LABEL_A)
        deep = [r for r in node.receptions if r.hops == 4]
        self.assertEqual(len(deep), 1, msg="fixture A carries exactly one 4-hop frame")
        self.assertEqual(
            deep[0].path,
            ["OE0EEE-5", "OE0FFF-6", "OE0GGG-7", "OE0HHH-8", "OE0BBB-2"],
            msg="the 4-hop path must be split into its five callsigns",
        )
        self.assertEqual(deep[0].dest, "H", msg="HEY destination of a non-gateway originator")

    # ------------------------------------------------------------------
    # Cross-node view: one msg_id must be heard by both logs.
    # ------------------------------------------------------------------

    def test_union_of_msg_ids_across_both_logs(self) -> None:
        cross = self.res["07_cross_node"]
        self.assertEqual(cross["union_msg_ids"], 12, msg="union over both fixtures")
        self.assertEqual(
            cross["venn"],
            {LABEL_A: 6, f"{LABEL_A}+{LABEL_B}": 3, LABEL_B: 3},
            msg="6 msg_ids only in A, 3 in both, 3 only in B",
        )

    def test_shared_msg_id_is_heard_by_both(self) -> None:
        ids_a = {r.msg_id for r in self._node(LABEL_A).receptions}
        ids_b = {r.msg_id for r in self._node(LABEL_B).receptions}
        self.assertIn("11110001", ids_a & ids_b, msg="the text message must be in both logs")

    # ------------------------------------------------------------------
    # Hop-counter wrap: max_hop is a 4-bit field, so H == 15 on a relayed
    # frame means its last hop forwarded a frame that arrived with max_hop 0.
    # The culprit's hardware falls back to the LH byte when it never
    # originates traffic of its own.
    # ------------------------------------------------------------------

    def test_wrap_culprit_detection(self) -> None:
        rows = self.res["22_wrap_culprits"]["rows"]
        got = {r["relayer"]: r["wrapping_frames"] for r in rows}
        self.assertEqual(
            got,
            {"OE0KKK-11": 1, "OE0EEE-5": 1},
            msg="both H0F frames must be attributed to their own last hop",
        )
        self.assertEqual(
            self.res["22_wrap_culprits"]["wrapped_msg_ids"],
            ["11110009"],
            msg="only the telemetry message wrapped its hop counter",
        )
        by_call = {r["relayer"]: r for r in rows}
        self.assertIn(
            "from LH byte",
            by_call["OE0KKK-11"]["hw"],
            msg="OE0KKK-11 never originates, so its board comes from the LH byte",
        )
        self.assertEqual(
            by_call["OE0EEE-5"]["fw"],
            "35:k",
            msg="OE0EEE-5 does originate (the 4-hop HEY), so its FW string is observable",
        )

    # ------------------------------------------------------------------
    # Own-relay echo: the [LOG] line is printed before the own-transmission
    # check, so a frame carrying our own callsign one position from the end
    # is our relay coming back from a neighbour.
    # ------------------------------------------------------------------

    def test_own_relay_echoes(self) -> None:
        echo = self.res["16_own_echo"]
        for label in (LABEL_A, LABEL_B):
            self.assertEqual(
                echo[label]["own_relay_echoed_by_neighbour"],
                3,
                msg=f"{label}: three frames carry the own callsign at path[-2]",
            )
            self.assertEqual(
                echo[label]["own_call_as_last_hop"],
                0,
                msg=f"{label}: a node can never be the last hop of a frame it received",
            )

    # ------------------------------------------------------------------
    # Time signal: two beacons exist, only A heard the second one.
    # ------------------------------------------------------------------

    def test_time_signal_beacons(self) -> None:
        ts = self.res["09_time_signal"]
        self.assertEqual(ts["union_beacons"], 2, msg="two distinct {CET} beacons in the fixtures")
        self.assertEqual(ts["per_node"][LABEL_A]["missed_count"], 0, msg="A heard both beacons")
        self.assertEqual(ts["per_node"][LABEL_B]["missed_count"], 1, msg="B missed the second beacon")

    # ------------------------------------------------------------------
    # Every percentage table must sum to 100 +- 0.5 (rounding slack only).
    # ------------------------------------------------------------------

    def test_percentage_tables_sum_to_100(self) -> None:
        for section in ("02_types", "05_neighbours", "19_first_relayer"):
            for label, block in self.res[section].items():
                if label.startswith("_") or not block.get("rows"):
                    continue
                total = block["pct_sum"]
                self.assertAlmostEqual(
                    total,
                    100.0,
                    delta=0.5,
                    msg=f"{section}[{label}] percentages sum to {total}, not 100 +- 0.5",
                )

    def test_first_copies_account_for_every_msg_id(self) -> None:
        for label, block in self.res["19_first_relayer"].items():
            got = sum(r["first_copies"] for r in block["rows"])
            self.assertEqual(
                got,
                block["msg_ids"],
                msg=f"{label}: every msg_id must have exactly one first copy",
            )

    # ------------------------------------------------------------------
    # End to end: the documented CLI writes both artefacts and its own
    # verification block re-derives the numbers with shell pipelines.
    # ------------------------------------------------------------------

    def test_cli_writes_json_and_markdown(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(FIXTURE_A), str(FIXTURE_B), "--out", str(out)],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=f"CLI exited {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            json_path = out / "berg.json"
            md_path = out / "berg.md"
            self.assertTrue(json_path.is_file(), msg="berg.json was not written")
            self.assertTrue(md_path.is_file(), msg="berg.md was not written")

            data = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(
                data["01_overview"][LABEL_A]["own_call"],
                "OE0AAA-1",
                msg="the CLI run must derive the same own call as the in-process run",
            )
            self.assertEqual(data["07_cross_node"]["union_msg_ids"], 12, msg="union in the CLI output")

            checks = data["_verification"]
            self.assertTrue(checks, msg="the verification block must not be empty")
            failed = [c["check"] for c in checks if not c["match"]]
            self.assertEqual(failed, [], msg=f"shell cross-checks disagreed with the analyser: {failed}")

            md = md_path.read_text(encoding="utf-8")
            self.assertNotIn("MISMATCH", md, msg="berg.md reports a failed verification row")
            for heading in ("## 1. Overview per log", "## 22. Hop-counter wrap culprits"):
                self.assertIn(heading, md, msg=f"berg.md is missing the section {heading!r}")


if __name__ == "__main__":
    unittest.main()
