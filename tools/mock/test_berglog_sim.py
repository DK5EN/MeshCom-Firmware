#!/usr/bin/env python3
"""Regression tests for tools/berglog_sim.py -- relay-policy replay.

Stdlib unittest only -- pytest is NOT available in this environment.

Expected values come from the SPECIFICATION of the hand-built graph below and
from the fixture spec in tools/mock/test_berglog.py, never from running the code
under test.

Run with:
    python3 -m unittest discover -s tools/mock -p 'test_berglog_sim.py'
    python3 tools/mock/test_berglog_sim.py

Hand-built graph (`toy_graph()`)
--------------------------------
Three shapes glued together, all edges directed "A->B means B hears A"::

    STAR      HUB -> L1, L2          L1 and L2 hear nobody else -> in-degree 1
                                     => HUB is their SOLE PROVIDER

    TRIANGLE  T1 <-> T2 <-> T3 <-> T1   fully reciprocal

    CHAIN     C1 -> C2 -> C3           each hears only its predecessor

    GLUE      ORIG -> HUB, ORIG -> T1, ORIG -> C1
              HUB  -> T1                 the hub also feeds the triangle
              T1   -> HUB                and hears it back, so the HUB CAN overhear

Facts the tests rely on, all readable off the diagram:
    DOUBLE-COVERED  HUB -> V, T1 -> V        V hears BOTH hubs, reaches nobody
                    HUB -> W, T1 -> W        W hears BOTH hubs ...
                    W   -> Z                 ... and is Z's only provider

Facts the tests rely on, all readable off the diagram:
  - 13 stations; ORIG has in-degree 0 and out-degree 3
  - in-degrees: ORIG 0 | HUB 2 (ORIG, T1) | T1 4 | T2 2 | T3 2 | V 2 | W 2
                L1 1, L2 1 (HUB) | C1 1 (ORIG) | C2 1 (C1) | C3 1 (C2) | Z 1 (W)
  - strict leaves (in-degree 1): C1, C2, C3, L1, L2, Z
  - HUB's exclusive leaves: L1, L2 -- their only in-edge is HUB
  - W's exclusive leaf: Z
  - audiences: HUB 5, T1 5, ORIG 3, T2 2, T3 2, C1 1, C2 1, W 1, rest 0
    -> HUB and T1 are the only top-tier (hub) stations
  - a flood from ORIG with hop limit 3 reaches every node
  - under P2 the HUB cancels whenever T1 relays first, and L1/L2 then go dark
  - under P3 the HUB may not cancel (sole provider), so L1/L2 always stay covered
  - V and W both hear the two hubs fire at roughly the same moment, a good 10 s
    before their own late slot, so a P5 station has exactly 1 counted copy: it
    goes silent at k=1 and transmits at k=2 and k=3
  - with veto="none" a silent W takes Z down with it; P5h instead lets W transmit
    with max_hop capped at 1, so Z still gets its copy

The T1 -> HUB edge is what makes the P2/P3 difference observable: without it the
HUB hears only ORIG, never overhears a second copy, and would never cancel.
"""

from __future__ import annotations

import json
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

REPO_ROOT: Path = Path(__file__).resolve().parents[2]
SCRIPT: Path = REPO_ROOT / "tools" / "berglog_sim.py"
FIXTURE_A: Path = REPO_ROOT / "tools" / "testdata" / "berglog_sample_a.txt"
FIXTURE_B: Path = REPO_ROOT / "tools" / "testdata" / "berglog_sample_b.txt"

sys.path.insert(0, str(REPO_ROOT / "tools"))
import berglog  # noqa: E402  (path is set up above)
import berglog_sim as sim  # noqa: E402

TOY_EDGES: tuple[tuple[str, str], ...] = (
    ("ORIG", "HUB"),
    ("ORIG", "T1"),
    ("ORIG", "C1"),
    ("HUB", "L1"),
    ("HUB", "L2"),
    ("HUB", "T1"),
    ("T1", "T2"),
    ("T2", "T1"),
    ("T2", "T3"),
    ("T3", "T2"),
    ("T3", "T1"),
    ("T1", "T3"),
    ("T1", "HUB"),
    ("C1", "C2"),
    ("C2", "C3"),
    # two stations that hear BOTH hubs, so a postponed decision has something to count
    ("HUB", "V"),
    ("T1", "V"),
    ("HUB", "W"),
    ("T1", "W"),
    ("W", "Z"),
)


def toy_graph() -> sim.HearingGraph:
    """Build the documented star + triangle + chain graph directly."""
    return build_graph_from(TOY_EDGES)


#: A chain deep enough that the yielding station still holds a budget of 2, so
#: capping it at 1 is observable. ORIG reaches A and the hub B; B also reaches A,
#: so A counts one copy before its own late slot and yields. A's budget at that
#: moment is 2 (it heard ORIG directly with max_hop 3), so an uncapped yield would
#: let N relay as well.
YIELD_EDGES: tuple[tuple[str, str], ...] = (
    ("ORIG", "A"),
    ("ORIG", "B"),
    ("B", "A"),
    ("B", "H1"),
    ("B", "H2"),
    ("A", "M"),
    ("M", "N"),
)


def build_graph_from(edges: tuple[tuple[str, str], ...]) -> sim.HearingGraph:
    calls = sorted({c for edge in edges for c in edge})
    index = {c: i for i, c in enumerate(calls)}
    out_sets: list[set[int]] = [set() for _ in calls]
    in_sets: list[set[int]] = [set() for _ in calls]
    weight: dict[tuple[int, int], int] = {}
    for a, b in edges:
        ia, ib = index[a], index[b]
        out_sets[ia].add(ib)
        in_sets[ib].add(ia)
        weight[(ia, ib)] = 1
    return sim.HearingGraph(
        calls=calls,
        index=index,
        out_adj=[sorted(x) for x in out_sets],
        in_adj=[sorted(x) for x in in_sets],
        weight=weight,
        frames_observed={c: 10 for c in calls},
    )


def toy_message(graph: sim.HearingGraph, hop_limit: int = 3) -> sim.Message:
    return sim.Message(
        msg_id="TOY00001",
        origin=graph.index["ORIG"],
        hop_limit=hop_limit,
        ptype=":",
        airtime_ms=1000.0,
    )


def run_many(
    graph: sim.HearingGraph,
    msg: sim.Message,
    policy: sim.Policy,
    tiers: list[int],
    seeds: range = range(1, 21),
) -> tuple[list[int], list[set[str]]]:
    """Run one message under one policy over many seeds."""
    excl = sim.exclusive_relay_set(graph, policy.veto)
    tx: list[int] = []
    reach: list[set[str]] = []
    for seed in seeds:
        out = sim.simulate(graph, msg, policy, tiers, excl, random.Random(seed))
        tx.append(out.transmissions)
        reach.append({graph.calls[i] for i in out.reached})
    return tx, reach


class TestToyGraph(unittest.TestCase):
    """Policy behaviour on the hand-built star / triangle / chain."""

    graph: sim.HearingGraph
    msg: sim.Message
    tiers: list[int]

    @classmethod
    def setUpClass(cls) -> None:
        cls.graph = toy_graph()
        cls.msg = toy_message(cls.graph)
        cls.tiers = sim.skew_tiers(cls.graph)

    def test_graph_shape_matches_the_diagram(self) -> None:
        g = self.graph
        self.assertEqual(len(g.calls), 13, msg="13 stations in the toy graph")
        self.assertEqual(g.in_degree("ORIG"), 0, msg="ORIG only ever transmits")
        self.assertEqual(g.audience("ORIG"), 3, msg="ORIG reaches HUB, T1 and C1")
        for leaf in ("L1", "L2"):
            self.assertEqual(g.in_degree(leaf), 1, msg=f"{leaf} hears only the HUB")
            self.assertEqual(g.in_adj[g.index[leaf]], [g.index["HUB"]], msg=f"{leaf}'s only provider is HUB")
        for tri in ("T2", "T3"):
            self.assertEqual(g.in_degree(tri), 2, msg=f"{tri} sits in the reciprocal triangle")
        self.assertEqual(g.in_degree("HUB"), 2, msg="HUB hears ORIG and T1, so it can overhear")
        self.assertEqual(g.audience("HUB"), 5, msg="HUB reaches L1, L2, T1, V and W")
        for both in ("V", "W"):
            self.assertEqual(
                [g.calls[j] for j in g.in_adj[g.index[both]]],
                ["HUB", "T1"],
                msg=f"{both} must hear both hubs, so a postponed decision has something to count",
            )
        self.assertEqual(g.in_degree("Z"), 1, msg="Z hangs off W alone")

    def test_exclusive_leaves(self) -> None:
        station: dict[str, dict[str, Any]] = {}
        leaves = sim.leaf_analysis(self.graph, station)
        self.assertEqual(
            sorted(leaves["strict_leaves"]),
            ["C1", "C2", "C3", "L1", "L2", "Z"],
            msg="strict leaves are the star leaves, the whole chain and Z",
        )
        by_relay = {r["relay"]: r for r in leaves["rows"]}
        self.assertEqual(
            sorted(by_relay["HUB"]["exclusive_leaves"]),
            ["L1", "L2"],
            msg="HUB is the sole provider of L1 and L2",
        )

    def test_p0_reaches_everything(self) -> None:
        reach = sim.p0_reach(self.graph, self.msg)
        self.assertEqual(
            {self.graph.calls[i] for i in reach},
            set(self.graph.calls),
            msg="hop limit 3 from ORIG covers the whole toy graph",
        )

    def test_p1_transmits_exactly_as_often_as_p0(self) -> None:
        p0 = next(p for p in sim.POLICIES if p.name == "P0")
        p1 = next(p for p in sim.POLICIES if p.name == "P1")
        tx0, _ = run_many(self.graph, self.msg, p0, self.tiers)
        tx1, _ = run_many(self.graph, self.msg, p1, self.tiers)
        self.assertEqual(
            sorted(set(tx0)),
            sorted(set(tx1)),
            msg=(
                "P1 only reorders relays, it never withdraws one, so the transmission "
                f"count must be unchanged. P0 {sorted(set(tx0))} vs P1 {sorted(set(tx1))}"
            ),
        )

    def test_p2_transmits_less_than_p0(self) -> None:
        p0 = next(p for p in sim.POLICIES if p.name == "P0")
        p2 = next(p for p in sim.POLICIES if p.name == "P2")
        tx0, _ = run_many(self.graph, self.msg, p0, self.tiers)
        tx2, _ = run_many(self.graph, self.msg, p2, self.tiers)
        self.assertLess(
            max(tx2),
            min(tx0),
            msg=f"cancel-on-overhear must cut transmissions: P0 {set(tx0)} vs P2 {set(tx2)}",
        )

    def test_p2_loses_the_exclusive_leaves_and_p3_keeps_them(self) -> None:
        p2 = next(p for p in sim.POLICIES if p.name == "P2")
        p3 = next(p for p in sim.POLICIES if p.name == "P3")
        _, reach2 = run_many(self.graph, self.msg, p2, self.tiers)
        _, reach3 = run_many(self.graph, self.msg, p3, self.tiers)
        lost2 = sum(1 for r in reach2 if not {"L1", "L2"} <= r)
        lost3 = sum(1 for r in reach3 if not {"L1", "L2"} <= r)
        self.assertGreater(
            lost2,
            0,
            msg="without the sole-provider veto the HUB cancels and L1/L2 go dark at least once",
        )
        self.assertEqual(
            lost3,
            0,
            msg=(
                "with the sole-provider veto the HUB must always relay, so L1 and L2 stay "
                f"covered in every run (lost in {lost3} of {len(reach3)})"
            ),
        )

    def test_p4_saves_more_than_p3_without_losing_the_star(self) -> None:
        p3 = next(p for p in sim.POLICIES if p.name == "P3")
        p4 = next(p for p in sim.POLICIES if p.name == "P4")
        tx3, _ = run_many(self.graph, self.msg, p3, self.tiers)
        tx4, reach4 = run_many(self.graph, self.msg, p4, self.tiers)
        self.assertLess(
            sum(tx4) / len(tx4),
            sum(tx3) / len(tx3),
            msg=f"the adaptive hop limit must cut transmissions further: P3 {set(tx3)} vs P4 {set(tx4)}",
        )
        lost = sum(1 for r in reach4 if not {"HUB", "L1", "L2"} <= r)
        self.assertEqual(
            lost,
            0,
            msg=f"P4 keeps the sole-provider veto, so the star stays covered (lost in {lost} runs)",
        )

    def test_veto_relays_are_only_counted_when_a_cancel_was_blocked(self) -> None:
        p3 = next(p for p in sim.POLICIES if p.name == "P3")
        p0 = next(p for p in sim.POLICIES if p.name == "P0")
        excl3 = sim.exclusive_relay_set(self.graph, p3.veto)
        excl0 = sim.exclusive_relay_set(self.graph, p0.veto)
        self.assertIn(self.graph.index["HUB"], excl3, msg="HUB is a sole provider")
        self.assertEqual(excl0, frozenset(), msg="P0 has no veto set at all")
        out0 = sim.simulate(self.graph, self.msg, p0, self.tiers, excl0, random.Random(7))
        self.assertEqual(out0.veto_relays, 0, msg="a policy without cancelling can never veto")

    def test_cancel_guard_blocks_late_cancels(self) -> None:
        loose = sim.Policy("g0", "guard 0", skew=True, cancel=True, cancel_guard_s=0.0)
        strict = sim.Policy("g9", "guard 9", skew=True, cancel=True, cancel_guard_s=9.0)
        tx_loose, _ = run_many(self.graph, self.msg, loose, self.tiers)
        tx_strict, _ = run_many(self.graph, self.msg, strict, self.tiers)
        self.assertLess(
            sum(tx_loose) / len(tx_loose),
            sum(tx_strict) / len(tx_strict),
            msg=(
                "a guard wider than any inter-generation gap must block every cancel, so the "
                f"strict variant transmits more: {set(tx_loose)} vs {set(tx_strict)}"
            ),
        )


class TestPostponePolicies(unittest.TestCase):
    """P5 / P5h -- 'sending is always allowed, just not immediately'."""

    graph: sim.HearingGraph
    msg: sim.Message
    tiers: list[int]
    seeds = range(1, 61)

    @classmethod
    def setUpClass(cls) -> None:
        cls.graph = toy_graph()
        cls.msg = toy_message(cls.graph)
        cls.tiers = sim.skew_tiers(cls.graph)

    def _p5(self, k: int, veto: str = "strict", hop_yield: bool = False) -> sim.Policy:
        return sim.Policy(
            "P5h" if hop_yield else "P5",
            f"k={k} veto={veto}",
            veto=veto,
            postpone_k=k,
            late_offset_s=6.0,
            cancel_guard_s=sim.POSTPONE_GUARD_S,
            hop_yield=hop_yield,
        )

    def test_p5_k1_without_veto_is_exactly_p2(self) -> None:
        """Deciding at the slot with k=1 is the same rule as cancelling on the
        first overhear -- as long as the timing and the guard match. The P5 delay
        is base + late_offset for tier 1 and base + late_offset + step for tier 2,
        so late=1.0 s with step=1.5 s reproduces P2's (0, 1.0, 2.5) tier skew."""
        p2 = next(p for p in sim.POLICIES if p.name == "P2")
        p5eq = sim.Policy(
            "P5eq",
            "P5 k=1, no veto, P2 timing",
            veto="none",
            postpone_k=1,
            late_offset_s=1.0,
            skew_step_s=1.5,
            cancel_guard_s=0.0,
        )
        tx2, _ = run_many(self.graph, self.msg, p2, self.tiers, self.seeds)
        tx5, _ = run_many(self.graph, self.msg, p5eq, self.tiers, self.seeds)
        self.assertEqual(
            tx2,
            tx5,
            msg=(
                "P5 with k=1 and no veto must reproduce P2 seed for seed. If this fails, the "
                "postpone check is no longer universal (a hub branch would exempt exactly the "
                f"stations P2 cancels). P2 {sorted(set(tx2))} vs P5 {sorted(set(tx5))}"
            ),
        )

    def test_transmissions_grow_monotonically_with_k(self) -> None:
        means = []
        for k in (1, 2, 3):
            tx, _ = run_many(self.graph, self.msg, self._p5(k), self.tiers, self.seeds)
            means.append(sum(tx) / len(tx))
        self.assertLessEqual(means[0], means[1], msg=f"k=1 must not transmit more than k=2: {means}")
        self.assertLessEqual(means[1], means[2], msg=f"k=2 must not transmit more than k=3: {means}")
        self.assertLess(
            means[0],
            means[2],
            msg=f"the k sweep must actually bite somewhere on this graph: {means}",
        )

    def test_p5h_coverage_is_at_least_p5_coverage(self) -> None:
        """W hears both hubs, so at k=1 it goes silent -- and takes Z, its only
        dependant, with it. P5h yields the hop budget instead, so Z still gets a copy."""
        for k in (1, 2, 3):
            _, reach5 = run_many(self.graph, self.msg, self._p5(k, veto="none"), self.tiers, self.seeds)
            _, reachh = run_many(
                self.graph, self.msg, self._p5(k, veto="none", hop_yield=True), self.tiers, self.seeds
            )
            cov5 = sum(len(r) for r in reach5) / len(reach5)
            covh = sum(len(r) for r in reachh) / len(reachh)
            self.assertGreaterEqual(
                covh,
                cov5,
                msg=f"k={k}: hop-yield must never cover less than going silent ({covh} < {cov5})",
            )
        lost5 = sum(
            1
            for r in run_many(self.graph, self.msg, self._p5(1, veto="none"), self.tiers, self.seeds)[1]
            if "Z" not in r
        )
        losth = sum(
            1
            for r in run_many(
                self.graph, self.msg, self._p5(1, veto="none", hop_yield=True), self.tiers, self.seeds
            )[1]
            if "Z" not in r
        )
        self.assertGreater(lost5, 0, msg="at k=1 without a veto, silent W must strand Z")
        self.assertEqual(losth, 0, msg="P5h must keep Z covered in every run")

    def test_sole_provider_veto_saves_z_without_hop_yield(self) -> None:
        _, reach = run_many(self.graph, self.msg, self._p5(1, veto="strict"), self.tiers, self.seeds)
        self.assertEqual(
            sum(1 for r in reach if "Z" not in r),
            0,
            msg="the strict veto must protect W, because it is Z's only provider",
        )

    def test_hop_yield_caps_the_budget_at_one_hop(self) -> None:
        """A yielding station puts max_hop 1 on the air, not its full budget.

        In YIELD_EDGES the station A still holds a budget of 2 when it yields, so
        an uncapped yield would let N relay one hop further. With the cap, M
        receives max_hop 1, relays with 0, and N stays silent."""
        graph = build_graph_from(YIELD_EDGES)
        tiers = sim.skew_tiers(graph)
        self.assertEqual(tiers[graph.index["B"]], 0, msg="B must be the hub that fires first")
        self.assertNotEqual(tiers[graph.index["A"]], 0, msg="A must take the late slot")
        msg = sim.Message("YIELD001", graph.index["ORIG"], 3, ":", 1000.0)
        policy = sim.Policy(
            "P5h",
            "yield",
            veto="none",
            postpone_k=1,
            late_offset_s=6.0,
            cancel_guard_s=sim.POSTPONE_GUARD_S,
            hop_yield=True,
        )
        excl = sim.exclusive_relay_set(graph, policy.veto)
        senders: list[frozenset[str]] = []
        for seed in range(1, 41):
            out = sim.simulate(graph, msg, policy, tiers, excl, random.Random(seed))
            self.assertGreaterEqual(out.hop_yields, 1, msg="A must actually yield")
            senders.append(frozenset({graph.calls[i] for i in out.reached}))
        for reached in senders:
            self.assertIn("N", reached, msg="the capped yield still delivers one hop past M")
        tx = [
            sim.simulate(graph, msg, policy, tiers, excl, random.Random(seed)).transmissions
            for seed in range(1, 41)
        ]
        self.assertEqual(
            sorted(set(tx)),
            [6],
            msg=(
                "ORIG, B, A, M, H1, H2 transmit; N receives max_hop 0 and must stay silent. "
                f"A larger count means the yield did not cap the budget at 1. Got {sorted(set(tx))}"
            ),
        )

    def test_grid_and_recommendation_shape(self) -> None:
        expected = (
            len(sim.POSTPONE_K_VALUES) * len(sim.LATE_OFFSET_VALUES) * len(sim.POSTPONE_VETOES) * 2
        )
        self.assertEqual(expected, 48, msg="the grid is k x late offset x veto, for P5 and P5h")


class TestOnFixtures(unittest.TestCase):
    """The analysis pipeline over the two synthetic berglog fixtures."""

    res: dict[str, Any] = {}

    @classmethod
    def setUpClass(cls) -> None:
        cls.res = sim.analyse([FIXTURE_A, FIXTURE_B], seeds=[1, 2])

    def test_graph_edges_follow_the_paths(self) -> None:
        nodes = berglog.parse_logs([FIXTURE_A, FIXTURE_B])
        graph = sim.build_graph([n for n in nodes if n.receptions])
        # fixture A: "OE0BBB-2,OE0AAA-1,OE0CCC-3" -> OE0AAA-1 heard OE0BBB-2, OE0CCC-3 heard OE0AAA-1
        self.assertIn(("OE0BBB-2", "OE0AAA-1"), {(graph.calls[a], graph.calls[b]) for a, b in graph.weight})
        self.assertIn(("OE0AAA-1", "OE0CCC-3"), {(graph.calls[a], graph.calls[b]) for a, b in graph.weight})
        # the logger itself hears the last hop of everything it received
        self.assertGreater(
            graph.in_degree("OE0AAA-1"), 0, msg="the logging node must have in-edges from its direct neighbours"
        )
        self.assertNotIn(
            ("OE0AAA-1", "OE0AAA-1"),
            {(graph.calls[a], graph.calls[b]) for a, b in graph.weight},
            msg="self-edges must be dropped",
        )

    def test_p1_matches_p0_on_the_fixtures(self) -> None:
        pol = {p["policy"]: p for p in self.res["04_simulation"]["policies"]}
        self.assertEqual(
            pol["P1"]["transmissions_total"]["mean"],
            pol["P0"]["transmissions_total"]["mean"],
            msg="skew without withdrawal cannot change the transmission count",
        )

    def test_cancelling_policies_transmit_less(self) -> None:
        pol = {p["policy"]: p for p in self.res["04_simulation"]["policies"]}
        for name in ("P2", "P3", "P4"):
            self.assertLess(
                pol[name]["transmissions_per_msg_mean"]["mean"],
                pol["P0"]["transmissions_per_msg_mean"]["mean"],
                msg=f"{name} must transmit less than P0",
            )
        self.assertLessEqual(
            pol["P2"]["transmissions_per_msg_mean"]["mean"],
            pol["P3"]["transmissions_per_msg_mean"]["mean"],
            msg="the sole-provider veto can only ADD relays back on top of P2",
        )

    def test_p0_reaches_the_whole_hop_limited_ball(self) -> None:
        pol = {p["policy"]: p for p in self.res["04_simulation"]["policies"]}
        self.assertAlmostEqual(
            pol["P0"]["coverage_mean_pct"]["mean"],
            100.0,
            delta=0.001,
            msg="the coverage denominator is the hop ball, and P0 must reach all of it",
        )

    def test_importance_uses_the_conservative_unknown_rule(self) -> None:
        nodes = berglog.parse_logs([FIXTURE_A, FIXTURE_B])
        graph = sim.build_graph([n for n in nodes if n.receptions])
        imp = sim.importance(graph, {})
        for call in graph.calls:
            self.assertAlmostEqual(
                imp[call]["importance"],
                float(graph.in_degree(call)),
                places=4,
                msg=(
                    "with no NC known at all, ADR 02 scores every heard neighbour 1.0, so "
                    f"importance must equal the in-degree for {call}"
                ),
            )
            self.assertEqual(imp[call]["nc_unknown_neighbours"], graph.in_degree(call))

    def test_measured_ordering_is_reported(self) -> None:
        overall = self.res["05_measured_ordering"]["overall"]
        self.assertGreaterEqual(overall["msg_ids_with_2plus_copies"], 1)
        self.assertLessEqual(
            overall["first_copy_from_largest_audience"],
            overall["msg_ids_with_2plus_copies"],
            msg="wins cannot exceed cases",
        )

    def test_postpone_grid_is_complete_and_recommended(self) -> None:
        grid = self.res["04b_postpone_grid"]
        self.assertEqual(
            len(grid["rows"]),
            len(sim.POSTPONE_K_VALUES) * len(sim.LATE_OFFSET_VALUES) * len(sim.POSTPONE_VETOES) * 2,
            msg="every grid point must be present",
        )
        for row in grid["rows"]:
            self.assertIn(row["family"], ("P5", "P5h"))
            self.assertLessEqual(
                row["coverage_mean_pct"]["mean"], 100.0001, msg="coverage cannot exceed 100 %"
            )
        self.assertEqual(sorted(grid["recommendation"]), [">= 95 % coverage", ">= 98 % coverage"])

    def test_audience_ranking_covers_the_named_stations(self) -> None:
        ar = self.res["04c_audience_ranking"]
        calls = {r["callsign"] for r in ar["rows"]}
        self.assertIn("OE3XIA-12", calls, msg="the brief names OE3XIA-12 explicitly")
        self.assertIn("OE3XPA-12", calls, msg="the brief names OE3XPA-12 explicitly")
        for row in ar["rows"]:
            if not row["present_in_graph"]:
                continue
            self.assertGreaterEqual(row["global_audience_rank"], 1)
            self.assertLessEqual(row["global_audience_rank"], row["of_nodes"])
            self.assertEqual(row["top_10pct_member"], row["skew_tier"] == 0)

    def test_assumptions_and_guesses_are_present(self) -> None:
        self.assertGreaterEqual(len(self.res["00_assumptions"]), 8)
        for item in self.res["00_assumptions"]:
            self.assertTrue(item["bias"], msg=f"assumption {item['assumption']!r} has no stated bias")
        self.assertGreaterEqual(len(self.res["06_guesses"]), 5)

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
                    "--seeds",
                    "2",
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
            self.assertTrue((out / "sim.json").is_file(), msg="sim.json was not written")
            self.assertTrue((out / "sim.md").is_file(), msg="sim.md was not written")
            data = json.loads((out / "sim.json").read_text(encoding="utf-8"))
            self.assertIn("01_hearing_graph", data)
            md = (out / "sim.md").read_text(encoding="utf-8")
            self.assertIn("## 4. Replay simulation", md)
            self.assertIn(
                "relays a little",
                md,
                msg="sim.md must quote the operator's concept verbatim",
            )


if __name__ == "__main__":
    unittest.main()
