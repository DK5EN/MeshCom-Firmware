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
FIXTURE_SETLOG_A: Path = (
    REPO_ROOT / "tools" / "testdata" / "berglog_sample_setlog_a.txt"
)
FIXTURE_SETLOG_B: Path = (
    REPO_ROOT / "tools" / "testdata" / "berglog_sample_setlog_b.txt"
)

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
            "04b_firmware": berglog.a04b_firmware(cls.nodes),
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
        res["24_relayer_firmware"] = berglog.a24_relayer_firmware(cls.nodes, res)
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
            self.assertEqual(
                len(node.undecodable), undecodable, msg=f"{label}: decode failures"
            )
            self.assertEqual(
                len({r.msg_id for r in node.receptions}),
                uniq,
                msg=f"{label}: unique msg_ids",
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
        expected = {
            LABEL_A: {":": 9, "@": 3, "!": 2},
            LABEL_B: {":": 6, "@": 2, "!": 1},
        }
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
        self.assertEqual(
            deep[0].dest, "H", msg="HEY destination of a non-gateway originator"
        )

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
        self.assertIn(
            "11110001", ids_a & ids_b, msg="the text message must be in both logs"
        )

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
        self.assertEqual(
            ts["union_beacons"], 2, msg="two distinct {CET} beacons in the fixtures"
        )
        self.assertEqual(
            ts["per_node"][LABEL_A]["missed_count"], 0, msg="A heard both beacons"
        )
        self.assertEqual(
            ts["per_node"][LABEL_B]["missed_count"], 1, msg="B missed the second beacon"
        )

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
    # Section 24: the FW field describes the ORIGINATOR, so a relayer's build
    # is only knowable when that station also originates frames we heard.
    # Fixture A gives all three buckets: OE0BBB-2 / OE0CCC-3 originate on 35:p
    # (CAD), OE0EEE-5 originates the 4-hop HEY on 35:k (pre-CAD), and
    # OE0KKK-11 relays the wrap frame but never originates (unknown).
    # ------------------------------------------------------------------

    def test_originator_firmware_is_learned_from_path0_only(self) -> None:
        fw = berglog.originator_firmware(self.nodes)
        self.assertEqual(
            fw["OE0EEE-5"]["fw"], "35:k", msg="OE0EEE-5 originates the 4-hop HEY"
        )
        self.assertFalse(
            fw["OE0EEE-5"]["fw_cad"],
            msg="35:k predates CAD -- this is what keeps the pre-CAD bucket non-empty",
        )
        self.assertTrue(fw["OE0BBB-2"]["fw_cad"], msg="OE0BBB-2 originates on 35:p")
        self.assertNotIn(
            "OE0KKK-11",
            fw,
            msg="OE0KKK-11 only ever relays, so it must have no firmware of its own",
        )

    def test_relayer_firmware_counts_only_relayed_copies(self) -> None:
        block = self.res["24_relayer_firmware"]["per_node"]
        expected = {LABEL_A: (11, 3), LABEL_B: (7, 2)}
        for label, (relayed, direct) in expected.items():
            self.assertEqual(
                block[label]["relayed_copies"], relayed, msg=f"{label}: relayed copies"
            )
            self.assertEqual(
                block[label]["direct_copies"],
                direct,
                msg=f"{label}: a path of length 1 is the originator's own transmission, not a relay",
            )
            self.assertEqual(
                block[label]["relayed_copies"] + block[label]["direct_copies"],
                len(self._node(label).receptions),
                msg=f"{label}: relayed + direct must account for every reception",
            )

    def test_relayer_cad_buckets(self) -> None:
        block = self.res["24_relayer_firmware"]["per_node"]
        expected = {
            LABEL_A: {"CAD": 8, "pre-CAD": 2, "unknown": 1},
            LABEL_B: {"CAD": 4, "pre-CAD": 3, "unknown": 0},
        }
        for label, buckets in expected.items():
            got = {b: v["copies"] for b, v in block[label]["summary"].items()}
            self.assertEqual(
                got, buckets, msg=f"{label}: relayed copies per firmware bucket"
            )
        pooled = self.res["24_relayer_firmware"]["pooled"]["summary"]
        self.assertEqual(
            {b: v["copies"] for b, v in pooled.items()},
            {"CAD": 12, "pre-CAD": 5, "unknown": 1},
            msg="pooled buckets are the sum of both logs",
        )

    def test_relayer_first_copy_split(self) -> None:
        block = self.res["24_relayer_firmware"]["per_node"]
        expected = {
            LABEL_A: (6, 3, {"CAD": 5, "pre-CAD": 0, "unknown": 1}),
            LABEL_B: (4, 2, {"CAD": 3, "pre-CAD": 1, "unknown": 0}),
        }
        for label, (relayed, direct, buckets) in expected.items():
            fc = block[label]["first_copy"]
            self.assertEqual(
                fc["first_copy_relayed"], relayed, msg=f"{label}: relayed first copies"
            )
            self.assertEqual(
                fc["first_copy_direct_from_originator"],
                direct,
                msg=f"{label}: first copies that came straight from the originator",
            )
            self.assertEqual(
                {b: v["copies"] for b, v in fc["summary"].items()},
                buckets,
                msg=f"{label}: who wins the race, by the relay's own firmware",
            )

    def test_relayer_percentages_sum_to_100(self) -> None:
        rf = self.res["24_relayer_firmware"]
        for label, block in rf["per_node"].items():
            for key in ("pct_sum", "pct_sum_airtime"):
                self.assertAlmostEqual(
                    block[key],
                    100.0,
                    delta=0.5,
                    msg=f"24[{label}].{key} = {block[key]}",
                )
            if block["first_copy"]["first_copy_relayed"]:
                self.assertAlmostEqual(
                    block["first_copy"]["pct_sum"],
                    100.0,
                    delta=0.5,
                    msg=f"24[{label}].first_copy = {block['first_copy']['pct_sum']}",
                )
        self.assertAlmostEqual(rf["pooled"]["pct_sum"], 100.0, delta=0.5)
        self.assertAlmostEqual(rf["pooled"]["pct_sum_airtime"], 100.0, delta=0.5)

    def test_relayers_without_own_firmware_are_named(self) -> None:
        rf = self.res["24_relayer_firmware"]
        self.assertEqual(
            rf["relayers_without_own_firmware"],
            ["OE0KKK-11"],
            msg="only the wrap relayer never originates in the fixtures",
        )
        pre = {r["relayer"]: r for r in rf["pre_cad_relayers"]}
        self.assertEqual(
            sorted(pre),
            ["OE0EEE-5"],
            msg="OE0EEE-5 is the only relayer that originates on a pre-CAD build",
        )
        self.assertEqual(pre["OE0EEE-5"]["fw"], "35:k")
        self.assertEqual(
            pre["OE0EEE-5"]["copies"], 5, msg="2 copies in log A plus 3 in log B"
        )
        self.assertIn(pre["OE0EEE-5"]["dominates"], (LABEL_A, LABEL_B))

    def test_originator_vs_relayer_view(self) -> None:
        rows = {
            r["log"]: r
            for r in self.res["24_relayer_firmware"]["originator_vs_relayer"]
        }
        self.assertEqual(sorted(rows), [LABEL_A, LABEL_B])
        for label, row in rows.items():
            block = self.res["24_relayer_firmware"]["per_node"][label]
            self.assertEqual(
                row["relayer_cad_pct"],
                block["summary"]["CAD"]["pct_copies"],
                msg=f"{label}: the combined view must quote the same relayer share",
            )
            self.assertEqual(
                row["originator_cad_pct"],
                self.res["04b_firmware"][label]["cad_receptions_pct"],
                msg=f"{label}: the originator share comes straight from section 4b",
            )

    # ------------------------------------------------------------------
    # End to end: the documented CLI writes both artefacts and its own
    # verification block re-derives the numbers with shell pipelines.
    # ------------------------------------------------------------------

    def test_cli_writes_json_and_markdown(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    str(FIXTURE_A),
                    str(FIXTURE_B),
                    "--out",
                    str(out),
                ],
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
            self.assertEqual(
                data["07_cross_node"]["union_msg_ids"],
                12,
                msg="union in the CLI output",
            )

            checks = data["_verification"]
            self.assertTrue(checks, msg="the verification block must not be empty")
            failed = [c["check"] for c in checks if not c["match"]]
            self.assertEqual(
                failed,
                [],
                msg=f"shell cross-checks disagreed with the analyser: {failed}",
            )

            md = md_path.read_text(encoding="utf-8")
            self.assertNotIn(
                "MISMATCH", md, msg="berg.md reports a failed verification row"
            )
            for heading in (
                "## 1. Overview per log",
                "## 22. Hop-counter wrap culprits",
            ):
                self.assertIn(
                    heading, md, msg=f"berg.md is missing the section {heading!r}"
                )


class TestBerglogSetlog(unittest.TestCase):
    """Regression tests for SL-07 -- the `--setlog` SL-01..SL-06 instrumentation.

    Fixture specification
    ----------------------
    ``berglog_sample_setlog_a.txt`` -- node OE0SLA-20, 30 lines, one of every new
    line kind plus a deliberate mix of RX lines with and without the SL-01 tail
    (``RSSI:``/``SNR:``/``DUP:``/``OWN:``/``t=``)::

        msg_id    kind      note
        20000001  RX (old)  direct from OE0SLB-21, no tail
        20000001  RX (new)  2nd copy via OE0SLB-21,OE0SLC-22, RSSI:-95 SNR:6 DUP:d OWN:-
        20000001  RX (new)  3rd copy, own-relay echo (OE0SLA-20 mid-path), DUP:d OWN:e
        20000002  RX (new)  position, DUP:n
        20000003  RX (old)  direct from OE0SLD-23, no tail
        20000008  RX (new)  HEY, RSSI:-102 SNR:-2 (far/weak -> feeds the noise floor)
        20000009  RX (new)  text, RSSI:-130 SNR:-25 (far/weak -> feeds the noise floor)
        00000000  RX        FCS:0000 decode failure (unaffected by SL-01..06)

        => 7 receptions (5 with the SL-01 tail, 2 without), 1 decode failure,
           5 unique msg_ids, 2 copies (msg_id 20000001 seen 3x), matching the
           2 receptions whose DUP: field reads 'd'.

        RLY x6: reason tx x3 (types :, !, @), loop x1, nomesh x1, gwfilter x1
        TX  x5: prio 1 (n1, wait 800), prio 2 (n3, waits 1200/1400/1600),
                prio 3 (n1, wait 2200); src o (n1, 800), src r (n3, 1200/1400/2200),
                src g (n1, 1600)
        ERR x3: rssi/snr/len/ferr vary, duration_s 599.9 (00:00:00.100..00:10:00.000)
        STAT x2: newid 40 then 55, heap 152000 -> 149500, fw 35p/20260201
        GWI x2 (msg_id 20000010, 20000011), GWU x2

    ``berglog_sample_setlog_b.txt`` -- node OE0SLM-30, 16 lines, sharing msg_id
    20000001 (RX) and 20000010 (GWI) with fixture A::

        msg_id    kind      note
        20000001  RX (old)  direct-ish from OE0SLB-21, no tail (shared with A)
        20000001  RX (new)  2nd copy, RSSI:-99 SNR:4 DUP:d
        20000020  RX (new)  position, RSSI:-75 SNR:10 DUP:n

        => 3 receptions (2 tailed), 2 unique msg_ids, 1 copy (matches DUP:d count 1)

        RLY x2: tx x1, self x1
        TX  x2: prio 1 (src r, wait 900), prio 2 (src o, wait 2100)
        ERR x2, duration_s 599.85 (00:00:00.150..00:10:00.000)
        STAT x2: newid 22 then 30, heap 160000 -> 158000
        GWI x2: msg_id 20000010 (SHARED with A -- the gateway-multiplier case),
                msg_id 20000012 (unique to B)

        => gateway multiplier: msg_id 20000010 has 2 distinct GWI nodes (A, B);
           20000011 and 20000012 have 1 each; mean 4/3 = 1.33.

    Every number below is derived from that specification by hand (the same
    arithmetic ``a25_setlog()`` performs), not by running the analyser first.
    """

    nodes: list[berglog.NodeLog] = []
    a: berglog.NodeLog
    b: berglog.NodeLog
    res25: dict[str, Any] = {}

    @classmethod
    def setUpClass(cls) -> None:
        cls.nodes = berglog.parse_logs([FIXTURE_SETLOG_A, FIXTURE_SETLOG_B])
        cls.a = cls.nodes[0]
        cls.b = cls.nodes[1]
        cls.res25 = berglog.a25_setlog(cls.nodes, dedup_ring=100)

    # ------------------------------------------------------------------
    # Labels: both fixture stems start with "berglog", so the short label
    # collides and both fall back to their full stem -- proves the existing
    # clash-detection in assign_labels() also covers the new fixture pair.
    # ------------------------------------------------------------------

    def test_labels_fall_back_to_full_stem_on_clash(self) -> None:
        self.assertEqual(
            [n.label for n in self.nodes],
            ["berglog_sample_setlog_a", "berglog_sample_setlog_b"],
        )

    # ------------------------------------------------------------------
    # SL-01 backward compatibility: an RX line without the tail must parse
    # exactly like before (rssi/snr/dup/own_echo/t_ms all None), and a mixed
    # fixture of old- and new-style lines must give the SAME reception count
    # a purely-old fixture would -- the tail is optional, not required.
    # ------------------------------------------------------------------

    def test_mixed_old_new_rx_lines_give_same_reception_count(self) -> None:
        # 7 receptions total at A: 2 old-style (no tail) + 5 new-style (tail).
        self.assertEqual(len(self.a.receptions), 7)
        self.assertEqual(len(self.a.undecodable), 1)
        tailed = [r for r in self.a.receptions if r.has_rx_tail]
        untailed = [r for r in self.a.receptions if not r.has_rx_tail]
        self.assertEqual(len(tailed), 5)
        self.assertEqual(len(untailed), 2)
        # every reception counts once regardless of which style produced it
        self.assertEqual(len(tailed) + len(untailed), len(self.a.receptions))
        # at B: 1 old-style + 2 new-style = 3
        self.assertEqual(len(self.b.receptions), 3)
        b_tailed = [r for r in self.b.receptions if r.has_rx_tail]
        self.assertEqual(len(b_tailed), 2)

    def test_old_style_rx_line_has_no_tail_fields(self) -> None:
        old = [
            r for r in self.a.receptions if r.msg_id == "20000001" and not r.has_rx_tail
        ]
        self.assertEqual(len(old), 1, msg="exactly one old-style copy of 20000001 at A")
        r = old[0]
        self.assertIsNone(r.rssi)
        self.assertIsNone(r.snr)
        self.assertIsNone(r.dup)
        self.assertIsNone(r.own_echo)
        self.assertIsNone(r.t_ms)

    def test_new_style_rx_line_carries_dup_and_own_echo(self) -> None:
        copies = sorted(
            (r for r in self.a.receptions if r.msg_id == "20000001" and r.has_rx_tail),
            key=lambda r: r.host,
        )
        self.assertEqual(len(copies), 2)
        second, third = copies
        self.assertEqual(
            (second.rssi, second.snr, second.dup, second.own_echo),
            (-95, 6, True, False),
        )
        self.assertEqual(
            (third.rssi, third.snr, third.dup, third.own_echo), (-88, 9, True, True)
        )

    # ------------------------------------------------------------------
    # a25_setlog: RSSI/SNR distribution and noise-floor estimate.
    # ------------------------------------------------------------------

    def test_rssi_snr_distribution(self) -> None:
        rs = self.res25["per_node"]["berglog_sample_setlog_a"]["rssi_snr"]
        self.assertEqual(rs["n"], 5)
        self.assertEqual(rs["rssi_dbm"]["min"], -130.0)
        self.assertEqual(rs["rssi_dbm"]["max"], -70.0)
        self.assertEqual(rs["rssi_dbm"]["median"], -95.0)
        self.assertAlmostEqual(rs["rssi_dbm"]["mean"], -97.0)
        self.assertEqual(rs["snr_db"]["min"], -25.0)
        self.assertEqual(rs["snr_db"]["max"], 11.0)
        self.assertEqual(rs["snr_db"]["median"], 6.0)
        # noise floor: two tailed receptions have SNR < 0 (-2 and -25), giving
        # RSSI-SNR of -100 and -105 -- median -102.5
        self.assertEqual(rs["noise_floor_estimate_dbm"], -102.5)
        self.assertIn("SNR < 0", rs["noise_floor_method"])

    def test_noise_floor_falls_back_when_no_snr_below_zero(self) -> None:
        # B's two tailed receptions have SNR 4 and 10 -- neither is < 0, so the
        # estimate must fall back to "all receptions" and say so.
        rs = self.res25["per_node"]["berglog_sample_setlog_b"]["rssi_snr"]
        self.assertEqual(rs["noise_floor_estimate_dbm"], -94.0)
        self.assertIn("no SNR < 0", rs["noise_floor_method"])

    # ------------------------------------------------------------------
    # Copies vs DUP:d cross-check: the two independent methods must agree on
    # these fixtures (both count 2 at A, 1 at B).
    # ------------------------------------------------------------------

    def test_copies_vs_dup_cross_check(self) -> None:
        for label, expected in (
            ("berglog_sample_setlog_a", 2),
            ("berglog_sample_setlog_b", 1),
        ):
            cd = self.res25["per_node"][label]["copies_vs_dup"]
            self.assertEqual(cd["dup_field_count"], expected, msg=label)
            self.assertEqual(cd["copy_count_old_method"], expected, msg=label)

    # ------------------------------------------------------------------
    # SL-02 relay-reason histogram.
    # ------------------------------------------------------------------

    def test_relay_reason_histogram(self) -> None:
        rr = self.res25["per_node"]["berglog_sample_setlog_a"]["relay_reasons"]
        self.assertEqual(rr["total"], 6)
        self.assertEqual(
            rr["by_reason"], {"tx": 3, "loop": 1, "nomesh": 1, "gwfilter": 1}
        )
        self.assertEqual(
            rr["by_reason_and_type"],
            {
                "tx/:": 1,
                "tx/!": 1,
                "tx/@": 1,
                "loop/:": 1,
                "nomesh/:": 1,
                "gwfilter/:": 1,
            },
        )
        rr_b = self.res25["per_node"]["berglog_sample_setlog_b"]["relay_reasons"]
        self.assertEqual(rr_b["by_reason"], {"tx": 1, "self": 1})

    # ------------------------------------------------------------------
    # SL-03 TX wait-time distribution per prio and per src.
    # ------------------------------------------------------------------

    def test_tx_wait_by_prio(self) -> None:
        tw = self.res25["per_node"]["berglog_sample_setlog_a"]["tx_wait"]
        self.assertEqual(tw["total"], 5)
        self.assertEqual(
            tw["by_prio"]["1"],
            {"n": 1, "median_ms": 800.0, "p90_ms": 800.0, "max_ms": 800},
        )
        self.assertEqual(
            tw["by_prio"]["2"],
            {"n": 3, "median_ms": 1400.0, "p90_ms": 1560.0, "max_ms": 1600},
        )
        self.assertEqual(
            tw["by_prio"]["3"],
            {"n": 1, "median_ms": 2200.0, "p90_ms": 2200.0, "max_ms": 2200},
        )

    def test_tx_wait_by_src(self) -> None:
        tw = self.res25["per_node"]["berglog_sample_setlog_a"]["tx_wait"]
        self.assertEqual(
            tw["by_src"]["o"],
            {"n": 1, "median_ms": 800.0, "p90_ms": 800.0, "max_ms": 800},
        )
        self.assertEqual(
            tw["by_src"]["r"],
            {"n": 3, "median_ms": 1400.0, "p90_ms": 2040.0, "max_ms": 2200},
        )
        self.assertEqual(
            tw["by_src"]["g"],
            {"n": 1, "median_ms": 1600.0, "p90_ms": 1600.0, "max_ms": 1600},
        )

    # ------------------------------------------------------------------
    # SL-04 collision rate.
    # ------------------------------------------------------------------

    def test_collision_rate(self) -> None:
        co = self.res25["per_node"]["berglog_sample_setlog_a"]["collision"]
        self.assertEqual(co["err_count"], 3)
        # duration_s = 599.9 s (00:00:00.100 .. 00:10:00.000);
        # 3 / (599.9/3600) = 18.003 -> 18.0
        self.assertAlmostEqual(co["err_per_hour"], 18.0, places=1)
        # 3 ERR / (3 ERR + 7 RX) = 30 %
        self.assertEqual(co["err_pct_of_rx_plus_err"], 30.0)

        co_b = self.res25["per_node"]["berglog_sample_setlog_b"]["collision"]
        self.assertEqual(co_b["err_count"], 2)
        self.assertEqual(co_b["err_pct_of_rx_plus_err"], 40.0)  # 2 / (2 + 3)

    # ------------------------------------------------------------------
    # SL-05: channel util per 5 min, heap trend, fw, dedup window.
    # ------------------------------------------------------------------

    def test_channel_util_rows(self) -> None:
        cu = self.res25["per_node"]["berglog_sample_setlog_a"]["channel_util_5min"]
        self.assertEqual(
            cu,
            [
                {
                    "host": "2026-02-01 00:05:00.000",
                    "util_pct": 12,
                    "rx_ms": 15000,
                    "tx_ms": 3000,
                },
                {
                    "host": "2026-02-01 00:10:00.000",
                    "util_pct": 15,
                    "rx_ms": 18000,
                    "tx_ms": 4200,
                },
            ],
        )

    def test_heap_trend_and_fw(self) -> None:
        ht = self.res25["per_node"]["berglog_sample_setlog_a"]["heap_trend"]
        self.assertEqual(
            ht, {"first": 152000, "min": 149500, "last": 149500, "n_stat_lines": 2}
        )
        self.assertEqual(
            self.res25["per_node"]["berglog_sample_setlog_a"]["fw"], "35p/20260201"
        )

    def test_dedup_window_estimate(self) -> None:
        # newid 40 -> 100 * 300 / 40 = 750.0 s; newid 55 -> 100 * 300 / 55 = 545.5 s;
        # median of the two unrounded values = 647.7 s
        dw = self.res25["per_node"]["berglog_sample_setlog_a"]["dedup_window"]
        self.assertEqual(dw["dedup_ring_assumed"], 100)
        self.assertEqual(dw["window_s_per_interval"], [750.0, 545.5])
        self.assertAlmostEqual(dw["window_s_median"], 647.7, places=1)

        # A different --dedup-ring must scale the window linearly (board-dependent).
        res_ring50 = berglog.a25_setlog(self.nodes, dedup_ring=50)
        dw50 = res_ring50["per_node"]["berglog_sample_setlog_a"]["dedup_window"]
        self.assertAlmostEqual(
            dw50["window_s_median"], dw["window_s_median"] / 2.0, places=1
        )

    # ------------------------------------------------------------------
    # SL-06: gateway multiplier across the two node logs.
    # ------------------------------------------------------------------

    def test_gateway_multiplier_across_nodes(self) -> None:
        self.assertEqual(
            self.res25["per_node"]["berglog_sample_setlog_a"]["gwu_count"], 2
        )
        self.assertEqual(
            self.res25["per_node"]["berglog_sample_setlog_b"]["gwu_count"], 2
        )

        gm = self.res25["gateway_multiplier"]
        rows = {r["msg_id"]: r for r in gm["rows"]}
        self.assertEqual(
            rows["20000010"],
            {
                "msg_id": "20000010",
                "gwi_count": 2,
                "gwi_nodes": 2,
                "nodes": ["berglog_sample_setlog_a", "berglog_sample_setlog_b"],
            },
            msg="msg_id 20000010 was independently injected by both A and B",
        )
        self.assertEqual(rows["20000011"]["gwi_nodes"], 1)
        self.assertEqual(rows["20000011"]["nodes"], ["berglog_sample_setlog_a"])
        self.assertEqual(rows["20000012"]["gwi_nodes"], 1)
        self.assertEqual(rows["20000012"]["nodes"], ["berglog_sample_setlog_b"])
        # mean over [2, 1, 1]
        self.assertAlmostEqual(gm["mean_gwi_nodes_per_msgid"], 4.0 / 3.0, places=2)

    # ------------------------------------------------------------------
    # Firmware without SL-01..06 must report the capability gap, never a
    # misleading zero -- checked against the OLD fixtures (no [LOG] tail,
    # no RLY/TX/ERR/STAT/GWI/GWU lines at all).
    # ------------------------------------------------------------------

    def test_pre_setlog_firmware_reports_not_in_firmware(self) -> None:
        old_nodes = berglog.parse_logs([FIXTURE_A, FIXTURE_B])
        res = berglog.a25_setlog(old_nodes, dedup_ring=100)
        for label, block in res["per_node"].items():
            for key in (
                "rssi_snr",
                "copies_vs_dup",
                "relay_reasons",
                "tx_wait",
                "collision",
                "channel_util_5min",
                "heap_trend",
                "fw",
                "dedup_window",
                "gwu_count",
            ):
                self.assertEqual(
                    block[key],
                    berglog.NOT_IN_FIRMWARE,
                    msg=f"{label}.{key} must report the capability gap",
                )
        self.assertEqual(res["gateway_multiplier"], berglog.NOT_IN_FIRMWARE)

    # ------------------------------------------------------------------
    # End to end: the CLI must write section 25 into berg.md and every
    # verify() cross-check (including the ones this fixture pair exercises
    # differently -- unique msg_ids and redundant receptions, now that RLY/TX/
    # GWI/GWU lines also carry an "x<8 hex>" id that must NOT be counted as an
    # RX msg_id) must still agree with the shell pipelines.
    # ------------------------------------------------------------------

    def test_cli_writes_section_25_and_verification_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    str(FIXTURE_SETLOG_A),
                    str(FIXTURE_SETLOG_B),
                    "--out",
                    str(out),
                ],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=f"CLI exited {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            data = json.loads((out / "berg.json").read_text(encoding="utf-8"))
            checks = data["_verification"]
            self.assertTrue(checks)
            failed = [c["check"] for c in checks if not c["match"]]
            self.assertEqual(
                failed,
                [],
                msg=f"shell cross-checks disagreed with the analyser: {failed}",
            )
            self.assertEqual(
                data["07_cross_node"]["union_msg_ids"],
                6,
                msg="RX msg_ids only: {20000001,2,3,8,9} from A union {20000001,20000020} from B",
            )

            md = (out / "berg.md").read_text(encoding="utf-8")
            self.assertIn("## 25. setlog instrumentation (SL-01..SL-06)", md)
            self.assertIn("Gateway multiplier", md)
            self.assertNotIn("MISMATCH", md)

    def test_cli_with_explicit_dedup_ring_option(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    str(FIXTURE_SETLOG_A),
                    "--out",
                    str(out),
                    "--dedup-ring",
                    "200",
                ],
                capture_output=True,
                text=True,
                cwd=str(REPO_ROOT),
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            data = json.loads((out / "berg.json").read_text(encoding="utf-8"))
            # a single log has no label clash, so assign_labels() keeps the
            # short "berglog" label instead of falling back to the full stem.
            dw = data["25_setlog"]["per_node"]["berglog"]["dedup_window"]
            self.assertEqual(dw["dedup_ring_assumed"], 200)
            # doubling the assumed ring size must double the window estimate
            self.assertAlmostEqual(dw["window_s_median"], 647.7 * 2, places=0)


if __name__ == "__main__":
    unittest.main()
