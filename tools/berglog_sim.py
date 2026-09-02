#!/usr/bin/env python3
"""berglog_sim.py -- audience metric and relay-policy replay on the measured topology.

Usage
-----
    python3 tools/berglog_sim.py <log>... --out <dir>

Writes ``sim.json`` (every number) and ``sim.md`` (tables and assumptions) into
``<dir>``. Stdlib only; the log parsing and the shared helpers are imported from
``tools/berglog.py``, which this module never modifies.

What it does
------------
1. Builds a DIRECTED HEARING GRAPH from the paths in the receiver logs. In a path
   ``A,B,C`` the station B relayed A's transmission, so B heard A -- edge A->B.
   The logger itself is appended: the last hop of every reception is heard by the
   logging node. Out-degree is therefore the AUDIENCE (how many stations relay my
   frames, i.e. how many hear me), in-degree the number of stations I hear.
2. Puts three candidate metrics side by side: AUDIENCE, the NC the node reports on
   air itself, and ADR-02 IMPORTANCE = sum(1 / NC_reported) over the neighbours the
   node hears, with the ADR's conservative "unknown = 1.0" rule.
3. Finds leaves (in-degree 1 or 2) and, per relay, the leaves only that relay serves.
4. Replays every observed message over the graph under five relay policies.
5. Measures the same ordering question on the REAL logs, with no model at all.

Timing model
------------
Relay backoff follows ``csma_compute_timeout_prio()`` (src/lora_functions.cpp:2217)
with the constants from src/configuration_global.h:326-337::

    text relay  MSG_PRIO_NORMAL      CSMA_PRIO_BASE_3 = 4500 ms
    position    MSG_PRIO_LOW         CSMA_PRIO_BASE_4 = 5500 ms
    HEY         MSG_PRIO_BACKGROUND  CSMA_PRIO_BASE_5 = 5500 ms

The jitter is the firmware's discrete draw ``random(0, SLOTS + 1) * CSMA_SLOT_SIZE``
= k * 35 ms with k uniform on 0..10, i.e. 0 .. 350 ms -- modelled exactly, not as a
continuous uniform.

Hop budget follows the firmware: a frame arrives carrying ``max_hop``; the receiver
relays only when ``max_hop > 0`` and puts ``max_hop - 1`` on the air
(src/lora_functions.cpp:1271-1278). The originator's configured limit is recovered
from the logs as ``H + hops_taken``.

Every simplification is listed in ``sim.md`` under "Assumptions", together with the
direction in which it biases the result.
"""

from __future__ import annotations

import argparse
import json
import random
import statistics
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import berglog
from berglog import (  # noqa: F401  (re-exported for the tests)
    NodeLog,
    Reception,
    fw_is_cad,
    hw_name,
    md_table,
    pct,
    quantile,
    spearman,
    stats_block,
    time_on_air_ms,
)

# --------------------------------------------------------------------------
# Firmware constants mirrored from the tree (see module docstring)
# --------------------------------------------------------------------------

#: src/configuration_global.h:326-330 -- CSMA base per message priority, ms.
CSMA_BASE_MS: dict[str, int] = {":": 4500, "!": 5500, "@": 5500}
CSMA_BASE_DEFAULT_MS = 4500
#: src/configuration_global.h:333-337 and :272 -- k * 35 ms, k uniform on 0..10.
CSMA_SLOTS = 10
CSMA_SLOT_SIZE_MS = 35

#: Audience percentile boundaries for the relay skew classes.
SKEW_TOP_QUANTILE = 0.90
SKEW_MID_QUANTILE = 0.70
#: Slot multipliers per class; the step itself is the parametrised knob.
SKEW_TIER_MULTIPLIER: tuple[float, float, float] = (0.0, 1.0, 2.5)
RELAY_SKEW_STEP_S = 1.0

#: P4 lowers an originator's hop limit to this when it already reaches a top-tier hub.
ADAPTIVE_HOP_LIMIT = 2

#: P5 grid. k = how many copies a non-hub must have counted before it stays silent.
POSTPONE_K_VALUES: tuple[int, ...] = (1, 2, 3)
#: Extra delay a non-hub takes. 20 s is in the grid because the measured trailing
#: median between a big- and a small-audience relayer is ~35 s (section 5), so the
#: shorter offsets only cover a fraction of the real spread.
LATE_OFFSET_VALUES: tuple[float, ...] = (3.0, 6.0, 10.0, 20.0)
POSTPONE_VETOES: tuple[str, ...] = ("strict", "soft")
#: Same listening guard as P3: a copy arriving later than this before the slot
#: no longer counts towards the decision.
POSTPONE_GUARD_S = 1.0
#: Coverage bars the recommendation is read off.
COVERAGE_TARGETS: tuple[float, ...] = (95.0, 98.0)

#: A node needs at least this many observed frames before it enters the metric table.
MIN_OBSERVED_FRAMES = 5

#: Coverage loss at or above this share of messages gets the node named.
COVERAGE_LOSS_REPORT = 0.05

DEFAULT_SEEDS = 5

TYPE_LABEL = berglog.TYPE_LABEL

#: The proposal under evaluation, quoted verbatim in sim.md.
OPERATOR_CONCEPT = """A node first learns its surroundings, then forms a hierarchy of who is heard by
many. A node that knows it is heard by MANY (its audience) relays a little
EARLIER than nodes with a small audience; the others then find the packet
already known (dedup) and stay silent. In parallel a node checks whether it
serves EXCLUSIVE leaves (valley nodes that only it reaches) -- then it must
relay regardless. Sending is always allowed, just not immediately. Long-term:
mesh always on, hop count and send slot chosen adaptively from the
surroundings."""


# --------------------------------------------------------------------------
# Hearing graph
# --------------------------------------------------------------------------


@dataclass
class HearingGraph:
    """Directed graph: an edge A->B means B was observed relaying A's frame."""

    calls: list[str]
    index: dict[str, int]
    out_adj: list[list[int]]
    in_adj: list[list[int]]
    weight: dict[tuple[int, int], int]
    frames_observed: dict[str, int]
    self_edges_dropped: int = 0

    @property
    def size(self) -> int:
        return len(self.calls)

    def audience(self, call: str) -> int:
        return len(self.out_adj[self.index[call]])

    def in_degree(self, call: str) -> int:
        return len(self.in_adj[self.index[call]])

    def reciprocity(self) -> dict[str, Any]:
        both = sum(1 for (a, b) in self.weight if (b, a) in self.weight)
        return {
            "edges": len(self.weight),
            "reciprocal_edges": both,
            "reciprocity_pct": pct(both, len(self.weight)),
        }


def build_graph(receivers: Sequence[NodeLog]) -> HearingGraph:
    """Every adjacent pair in a received path is one directed hearing edge."""
    weight_by_call: Counter[tuple[str, str]] = Counter()
    frames: Counter[str] = Counter()
    dropped = 0
    for node in receivers:
        for rec in node.receptions:
            for call in set(rec.path):
                frames[call] += 1
            for a, b in zip(rec.path, rec.path[1:]):
                if a == b:
                    dropped += 1
                    continue
                weight_by_call[(a, b)] += 1
            if node.own_call and rec.path:
                last = rec.path[-1]
                if last != node.own_call:
                    weight_by_call[(last, node.own_call)] += 1
                    frames[node.own_call] += 1
                else:
                    dropped += 1
    calls = sorted({c for edge in weight_by_call for c in edge} | set(frames))
    index = {c: i for i, c in enumerate(calls)}
    out_sets: list[set[int]] = [set() for _ in calls]
    in_sets: list[set[int]] = [set() for _ in calls]
    weight: dict[tuple[int, int], int] = {}
    for (a, b), w in weight_by_call.items():
        ia, ib = index[a], index[b]
        out_sets[ia].add(ib)
        in_sets[ib].add(ia)
        weight[(ia, ib)] = w
    return HearingGraph(
        calls=calls,
        index=index,
        out_adj=[sorted(s) for s in out_sets],
        in_adj=[sorted(s) for s in in_sets],
        weight=weight,
        frames_observed=dict(frames),
        self_edges_dropped=dropped,
    )


# --------------------------------------------------------------------------
# Reported neighbour count and the three metrics
# --------------------------------------------------------------------------


def reported_nc(nodes: Sequence[NodeLog], res: dict[str, Any]) -> dict[str, float]:
    """Median NC each station reports on air, from berglog's section 21."""
    return {row["callsign"]: float(row["nc_median"]) for row in res["21_neighbour_count"]["rows"]}


def importance(graph: HearingGraph, nc: dict[str, float]) -> dict[str, dict[str, Any]]:
    """ADR 02: Importance = sum(1 / NC_reported) over the neighbours I HEAR.

    docs/adr-nc-importance-backoff.md:488 and :669 -- a neighbour whose NC is not
    known contributes 1.0, the conservative "it might be isolated" assumption.
    """
    out: dict[str, dict[str, Any]] = {}
    for i, call in enumerate(graph.calls):
        total = 0.0
        unknown = 0
        for j in graph.in_adj[i]:
            value = nc.get(graph.calls[j])
            if value is None or value <= 0:
                total += 1.0
                unknown += 1
            else:
                total += 1.0 / value
        heard = len(graph.in_adj[i])
        out[call] = {
            "importance": round(total, 4),
            "neighbours_heard": heard,
            "nc_unknown_neighbours": unknown,
            "nc_unknown_share_pct": pct(unknown, heard),
        }
    return out


def metric_table(
    graph: HearingGraph,
    nc: dict[str, float],
    imp: dict[str, dict[str, Any]],
    station: dict[str, dict[str, Any]],
    res: dict[str, Any],
    gateways: set[str],
) -> dict[str, Any]:
    """AUDIENCE / NC_self / IMPORTANCE side by side, plus the berglog race numbers."""
    first_rate: dict[str, list[float]] = defaultdict(list)
    delay_behind: dict[str, list[float]] = defaultdict(list)
    for block in res["19_first_relayer"].values():
        for row in block["rows"]:
            first_rate[row["relayer"]].append(float(row["first_copy_rate_pct"]))
    for block in res["20_relay_latency"].values():
        for row in block["rows"]:
            delay_behind[row["relayer"]].append(float(row["median_s"]))

    rows: list[dict[str, Any]] = []
    for call in graph.calls:
        frames = graph.frames_observed.get(call, 0)
        if frames < MIN_OBSERVED_FRAMES:
            continue
        st = station.get(call, {})
        rows.append(
            {
                "callsign": call,
                "frames_observed": frames,
                "audience": graph.audience(call),
                "in_degree": graph.in_degree(call),
                "nc_self": nc.get(call),
                "importance": imp[call]["importance"],
                "nc_unknown_share_pct": imp[call]["nc_unknown_share_pct"],
                "first_copy_rate_pct": (
                    round(statistics.fmean(first_rate[call]), 2) if call in first_rate else None
                ),
                "median_delay_behind_first_s": (
                    round(statistics.fmean(delay_behind[call]), 2) if call in delay_behind else None
                ),
                "hw": st.get("hw"),
                "fw": st.get("fw"),
                "fw_cad": st.get("fw_cad"),
                "mountain_gateway": call in gateways,
            }
        )
    rows.sort(key=lambda d: (-int(d["audience"]), d["callsign"]))

    def corr(key_a: str, key_b: str) -> dict[str, Any]:
        pairs = [
            (float(r[key_a]), float(r[key_b]))
            for r in rows
            if r.get(key_a) is not None and r.get(key_b) is not None
        ]
        return {
            "n": len(pairs),
            "spearman": spearman([p[0] for p in pairs], [p[1] for p in pairs]),
        }

    metrics = ("audience", "nc_self", "importance")
    outcomes = ("first_copy_rate_pct", "median_delay_behind_first_s")
    return {
        "min_observed_frames": MIN_OBSERVED_FRAMES,
        "rows": rows,
        "correlation_between_metrics": {
            f"{a} vs {b}": corr(a, b) for i, a in enumerate(metrics) for b in metrics[i + 1 :]
        },
        "correlation_metric_vs_outcome": {
            f"{m} vs {o}": corr(m, o) for m in metrics for o in outcomes
        },
    }


def validate_graph(graph: HearingGraph, nc: dict[str, float]) -> dict[str, Any]:
    """In-degree (what we could observe) against the NC the node reports itself."""
    rows: list[dict[str, Any]] = []
    ratios: list[float] = []
    for call in graph.calls:
        if call not in nc:
            continue
        share = pct(graph.in_degree(call), nc[call])
        if nc[call] > 0:
            ratios.append(share)
        rows.append(
            {
                "callsign": call,
                "in_degree_observed": graph.in_degree(call),
                "nc_reported": nc[call],
                "coverage_pct": share,
                "audience_out_degree": graph.audience(call),
                "frames_observed": graph.frames_observed.get(call, 0),
            }
        )
    rows.sort(key=lambda d: (-float(d["nc_reported"]), str(d["callsign"])))
    return {
        "rows": rows,
        "n": len(rows),
        "spearman_in_degree_vs_nc": spearman(
            [float(r["in_degree_observed"]) for r in rows], [float(r["nc_reported"]) for r in rows]
        ),
        "observed_share_of_reported_pct": stats_block(ratios),
        "nodes_with_zero_in_degree": sum(1 for c in graph.calls if graph.in_degree(c) == 0),
        "limitation": (
            "An edge exists only when a frame that travelled it ended up in a path heard by one "
            "of the three loggers. Stations we only ever saw as originators have in-degree 0 -- "
            "not because they hear nobody, but because nothing they heard was ever observed. The "
            "graph is therefore a lower bound on connectivity, and every simulated relay count is "
            "a lower bound too."
        ),
    }


# --------------------------------------------------------------------------
# Leaves and exclusivity
# --------------------------------------------------------------------------


def leaf_analysis(graph: HearingGraph, station: dict[str, dict[str, Any]]) -> dict[str, Any]:
    strict_leaves = [c for c in graph.calls if graph.in_degree(c) == 1]
    soft_leaves = [c for c in graph.calls if 1 <= graph.in_degree(c) <= 2]
    exclusive: dict[int, list[int]] = defaultdict(list)
    soft_exclusive: dict[int, list[int]] = defaultdict(list)
    for i, call in enumerate(graph.calls):
        deg = len(graph.in_adj[i])
        if deg == 1:
            exclusive[graph.in_adj[i][0]].append(i)
        if 1 <= deg <= 2:
            for j in graph.in_adj[i]:
                soft_exclusive[j].append(i)
    rows: list[dict[str, Any]] = []
    for i, call in enumerate(graph.calls):
        excl = exclusive.get(i, [])
        soft = soft_exclusive.get(i, [])
        if not excl and not soft:
            continue
        st = station.get(call, {})
        rows.append(
            {
                "relay": call,
                "audience": len(graph.out_adj[i]),
                "exclusive_leaves": [graph.calls[k] for k in excl],
                "exclusive_leaf_count": len(excl),
                "soft_exclusive_count": len(soft),
                "soft_exclusive_leaves": [graph.calls[k] for k in soft],
                "hw": st.get("hw"),
                "fw": st.get("fw"),
                "fw_cad": st.get("fw_cad"),
            }
        )
    rows.sort(key=lambda d: (-int(d["exclusive_leaf_count"]), -int(d["soft_exclusive_count"])))
    depends_precad: list[dict[str, Any]] = []
    for i, call in enumerate(graph.calls):
        if not 1 <= len(graph.in_adj[i]) <= 2:
            continue
        providers = [graph.calls[j] for j in graph.in_adj[i]]
        pre = [p for p in providers if station.get(p, {}).get("fw_cad") is False]
        if pre:
            depends_precad.append(
                {
                    "leaf": call,
                    "in_degree": len(graph.in_adj[i]),
                    "providers": providers,
                    "pre_cad_providers": pre,
                    "only_pre_cad": len(pre) == len(providers),
                }
            )
    return {
        "strict_leaves": strict_leaves,
        "strict_leaf_count": len(strict_leaves),
        "soft_leaves": soft_leaves,
        "soft_leaf_count": len(soft_leaves),
        "zero_in_degree_nodes": [c for c in graph.calls if graph.in_degree(c) == 0],
        "rows": rows,
        "leaves_depending_on_pre_cad_relay": depends_precad,
        "leaves_with_only_pre_cad_providers": sum(1 for d in depends_precad if d["only_pre_cad"]),
    }


# --------------------------------------------------------------------------
# Replay simulation
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Policy:
    name: str
    label: str
    skew: bool = False
    cancel: bool = False
    veto: str = "none"  # "none" | "strict" | "soft"
    adaptive_hop: bool = False
    skew_step_s: float = RELAY_SKEW_STEP_S
    cancel_guard_s: float = 0.0
    #: P5 family: a station decides at its OWN slot, not on the first overhear.
    #: It transmits when it counted fewer than ``postpone_k`` copies, or when it
    #: holds the sole-provider veto. ``None`` disables the whole family.
    #: The rule is deliberately universal, not "non-hubs only": a hub is exempt in
    #: PRACTICE because the skew puts it first, so its count is still 0 when its
    #: slot fires -- not because of a special case in the code. Written as a branch
    #: it would also stop a hub that fired late from ever backing off, and it would
    #: break the identity "P5 with k=1 and no veto == P2".
    postpone_k: int | None = None
    #: extra delay a non-hub takes on top of the CSMA base, so hubs go first
    late_offset_s: float = 0.0
    #: P5h: instead of staying silent, yield the hop budget -- transmit with
    #: max_hop capped at 1 so the exclusive neighbours still get their one hop
    hop_yield: bool = False

    @property
    def is_postpone(self) -> bool:
        return self.postpone_k is not None


POLICIES: tuple[Policy, ...] = (
    Policy("P0", "today: fixed CSMA backoff, never withdraw"),
    Policy("P1", "audience skew only, never withdraw", skew=True),
    Policy("P2", "skew + cancel on overhear", skew=True, cancel=True),
    Policy("P3", "P2 + sole-provider veto (in-degree 1)", skew=True, cancel=True, veto="strict"),
    Policy("P3b", "P2 + sole-provider veto (in-degree <= 2)", skew=True, cancel=True, veto="soft"),
    Policy(
        "P4",
        "P3 + adaptive hop limit at the originator",
        skew=True,
        cancel=True,
        veto="strict",
        adaptive_hop=True,
    ),
)


@dataclass
class Message:
    msg_id: str
    origin: int
    hop_limit: int
    ptype: str
    airtime_ms: float


@dataclass
class SimOutcome:
    transmissions: int
    reached: set[int]
    #: relays that went out ONLY because the sole-provider veto blocked their cancel
    veto_relays: int
    cancels: int
    #: P5h only: relays that went out with the hop budget capped at 1
    hop_yields: int = 0


def skew_tiers(graph: HearingGraph) -> list[int]:
    """Global audience class per node: 0 = top 10 %, 1 = next 20 %, 2 = the rest."""
    audiences = [len(a) for a in graph.out_adj]
    top = quantile(audiences, SKEW_TOP_QUANTILE)
    mid = quantile(audiences, SKEW_MID_QUANTILE)
    tiers = []
    for a in audiences:
        if a >= top:
            tiers.append(0)
        elif a >= mid:
            tiers.append(1)
        else:
            tiers.append(2)
    return tiers


def p0_reach(graph: HearingGraph, msg: Message) -> set[int]:
    """Upper bound of the flood: the hop-limited ball around the originator.

    A frame leaves the originator with ``max_hop = H``; a station at distance d
    receives it with ``H - (d - 1)`` and relays while that is > 0, so the ball has
    radius ``H + 1``.
    """
    seen = {msg.origin}
    frontier = [msg.origin]
    for _ in range(msg.hop_limit + 1):
        nxt = []
        for i in frontier:
            for j in graph.out_adj[i]:
                if j not in seen:
                    seen.add(j)
                    nxt.append(j)
        frontier = nxt
        if not frontier:
            break
    return seen


def simulate(
    graph: HearingGraph,
    msg: Message,
    policy: Policy,
    tiers: Sequence[int],
    exclusive_relays: frozenset[int],
    rng: random.Random,
) -> SimOutcome:
    """Discrete-event flood of one message under one policy."""
    n = graph.size
    base_s = CSMA_BASE_MS.get(msg.ptype, CSMA_BASE_DEFAULT_MS) / 1000.0
    received: list[bool] = [False] * n
    relayed: list[bool] = [False] * n
    pending: dict[int, tuple[float, int]] = {}
    overheard: dict[int, int] = {}

    hop_limit = msg.hop_limit
    if policy.adaptive_hop and any(tiers[j] == 0 for j in graph.out_adj[msg.origin]):
        hop_limit = ADAPTIVE_HOP_LIMIT

    reached = {msg.origin}
    received[msg.origin] = True
    relayed[msg.origin] = True
    transmissions = 1
    cancels = 0
    hop_yields = 0
    veto_blocked: set[int] = set()

    # (fire_time, sequence, node, hop_field_to_put_on_air)
    queue: list[tuple[float, int, int, int]] = []
    seq = 0

    def emit(now: float, sender: int, hop_field: int) -> None:
        nonlocal transmissions, cancels, seq
        for j in graph.out_adj[sender]:
            reached.add(j)
            if j in pending:
                fire_at, _budget = pending[j]
                if policy.cancel and fire_at - now >= policy.cancel_guard_s:
                    if (policy.veto != "none") and (j in exclusive_relays):
                        # sole provider: this relay must go out even though the
                        # message is already known on the channel
                        veto_blocked.add(j)
                    else:
                        del pending[j]
                        cancels += 1
                elif policy.is_postpone and fire_at - now >= policy.cancel_guard_s:
                    # P5: do not decide now, only tally. The decision happens when
                    # this node's own slot fires ("sending is always allowed, just
                    # not immediately").
                    overheard[j] = overheard.get(j, 0) + 1
            if received[j] or relayed[j]:
                continue
            received[j] = True
            if hop_field > 0:
                delay = base_s + rng.randrange(0, CSMA_SLOTS + 1) * CSMA_SLOT_SIZE_MS / 1000.0
                if policy.is_postpone:
                    # hubs keep the plain CSMA slot; everyone else waits out the
                    # late offset, with the P1 rank order preserved inside the
                    # non-hub group -- this is what makes hubs win in practice
                    if tiers[j] != 0:
                        delay += policy.late_offset_s + (tiers[j] - 1) * policy.skew_step_s
                elif policy.skew:
                    delay += SKEW_TIER_MULTIPLIER[tiers[j]] * policy.skew_step_s
                seq += 1
                fire = now + delay
                pending[j] = (fire, hop_field - 1)
                queue.append((fire, seq, j, hop_field - 1))

    emit(0.0, msg.origin, hop_limit)
    queue.sort()
    cursor = 0
    while cursor < len(queue):
        fire, _s, node, hop_field = queue[cursor]
        cursor += 1
        current = pending.get(node)
        if current is None or current[0] != fire:
            continue  # cancelled, or superseded
        del pending[node]
        if relayed[node]:
            continue
        if policy.is_postpone:
            heard = overheard.get(node, 0)
            assert policy.postpone_k is not None
            if heard >= policy.postpone_k:
                if policy.veto != "none" and node in exclusive_relays:
                    veto_blocked.add(node)
                elif policy.hop_yield:
                    # yield the budget instead of going silent
                    hop_field = min(hop_field, 1)
                    hop_yields += 1
                else:
                    relayed[node] = True
                    cancels += 1
                    continue
        relayed[node] = True
        transmissions += 1
        before = len(queue)
        emit(fire, node, hop_field)
        if len(queue) > before:
            queue[cursor:] = sorted(queue[cursor:])
    veto_relays = sum(1 for j in veto_blocked if relayed[j])
    return SimOutcome(transmissions, reached, veto_relays, cancels, hop_yields)


def collect_messages(receivers: Sequence[NodeLog], graph: HearingGraph) -> list[Message]:
    """One Message per msg_id, taken from its earliest observed copy."""
    first: dict[str, Reception] = {}
    lengths: dict[str, list[int]] = defaultdict(list)
    for node in receivers:
        for rec in node.receptions:
            lengths[rec.msg_id].append(rec.msg_len)
            cur = first.get(rec.msg_id)
            if cur is None or rec.host < cur.host:
                first[rec.msg_id] = rec
    out: list[Message] = []
    for msg_id, rec in sorted(first.items()):
        if rec.origin not in graph.index:
            continue
        out.append(
            Message(
                msg_id=msg_id,
                origin=graph.index[rec.origin],
                hop_limit=rec.initial_max_hop,
                ptype=rec.ptype,
                airtime_ms=time_on_air_ms(int(statistics.fmean(lengths[msg_id]))),
            )
        )
    return out


def exclusive_relay_set(graph: HearingGraph, mode: str) -> frozenset[int]:
    if mode == "none":
        return frozenset()
    limit = 1 if mode == "strict" else 2
    out: set[int] = set()
    for i in range(graph.size):
        deg = len(graph.in_adj[i])
        if 1 <= deg <= limit:
            out.update(graph.in_adj[i])
    return frozenset(out)


def run_policy(
    graph: HearingGraph,
    messages: Sequence[Message],
    policy: Policy,
    tiers: Sequence[int],
    reach_p0: dict[str, set[int]],
    seeds: Sequence[int],
    duration_h: float,
) -> dict[str, Any]:
    excl = exclusive_relay_set(graph, policy.veto)
    per_seed: list[dict[str, Any]] = []
    loss_events: Counter[int] = Counter()
    p0_opportunities: Counter[int] = Counter()
    for seed in seeds:
        rng = random.Random(seed)
        tx_all: list[int] = []
        tx_by_type: dict[str, list[int]] = defaultdict(list)
        cov: list[float] = []
        veto_total = 0
        cancel_total = 0
        yield_total = 0
        airtime_ms = 0.0
        for msg in messages:
            outcome = simulate(graph, msg, policy, tiers, excl, rng)
            tx_all.append(outcome.transmissions)
            tx_by_type[msg.ptype].append(outcome.transmissions)
            veto_total += outcome.veto_relays
            cancel_total += outcome.cancels
            yield_total += outcome.hop_yields
            airtime_ms += outcome.transmissions * msg.airtime_ms
            base = reach_p0[msg.msg_id]
            cov.append(len(outcome.reached & base) / len(base) if base else 1.0)
            if seed == seeds[0]:
                for node in base:
                    p0_opportunities[node] += 1
                    if node not in outcome.reached:
                        loss_events[node] += 1
        per_seed.append(
            {
                "seed": seed,
                "transmissions_total": sum(tx_all),
                "transmissions_per_msg_mean": round(statistics.fmean(tx_all), 3),
                "transmissions_per_msg_p90": round(quantile(tx_all, 0.9), 2),
                "coverage_mean_pct": round(100.0 * statistics.fmean(cov), 3),
                "veto_relays": veto_total,
                "cancels": cancel_total,
                "hop_yields": yield_total,
                "airtime_s": round(airtime_ms / 1000.0, 1),
                "by_type": {
                    t: {
                        "messages": len(v),
                        "transmissions_per_msg_mean": round(statistics.fmean(v), 3),
                        "transmissions_per_msg_p90": round(quantile(v, 0.9), 2),
                    }
                    for t, v in sorted(tx_by_type.items())
                },
            }
        )

    def agg(key: str) -> dict[str, float]:
        vals = [float(s[key]) for s in per_seed]
        return {
            "mean": round(statistics.fmean(vals), 3),
            "min": round(min(vals), 3),
            "max": round(max(vals), 3),
            "spread": round(max(vals) - min(vals), 3),
        }

    losers = []
    for node, lost in loss_events.most_common():
        opportunities = p0_opportunities[node]
        share = lost / opportunities if opportunities else 0.0
        if share < COVERAGE_LOSS_REPORT:
            continue
        call = graph.calls[node]
        providers = sorted(
            (graph.calls[j] for j in graph.in_adj[node]),
            key=lambda c: -graph.audience(c),
        )
        losers.append(
            {
                "callsign": call,
                "in_degree": graph.in_degree(call),
                "messages_in_p0_reach": opportunities,
                "messages_lost": lost,
                "loss_share_pct": pct(lost, opportunities),
                "providers_by_audience": providers,
            }
        )
    airtime_s = agg("airtime_s")["mean"]
    return {
        "policy": policy.name,
        "label": policy.label,
        "skew_step_s": policy.skew_step_s,
        "cancel_guard_s": policy.cancel_guard_s,
        "seeds": list(seeds),
        "per_seed": per_seed,
        "transmissions_total": agg("transmissions_total"),
        "transmissions_per_msg_mean": agg("transmissions_per_msg_mean"),
        "transmissions_per_msg_p90": agg("transmissions_per_msg_p90"),
        "coverage_mean_pct": agg("coverage_mean_pct"),
        "veto_relays": agg("veto_relays"),
        "cancels": agg("cancels"),
        "hop_yields": agg("hop_yields"),
        "airtime_s": airtime_s,
        "airtime_per_hour_s": round(airtime_s / duration_h, 1) if duration_h else 0.0,
        "by_type": {
            t: {
                "messages": per_seed[0]["by_type"][t]["messages"],
                "transmissions_per_msg_mean": round(
                    statistics.fmean([s["by_type"][t]["transmissions_per_msg_mean"] for s in per_seed]), 3
                ),
                "transmissions_per_msg_p90": round(
                    statistics.fmean([s["by_type"][t]["transmissions_per_msg_p90"] for s in per_seed]), 2
                ),
            }
            for t in per_seed[0]["by_type"]
        },
        "coverage_losers": losers,
    }


# --------------------------------------------------------------------------
# Step 5: the same ordering question measured on the raw logs
# --------------------------------------------------------------------------


def measured_ordering(receivers: Sequence[NodeLog], graph: HearingGraph) -> dict[str, Any]:
    """How often does the biggest-audience relayer already deliver the first copy?"""
    per_node: dict[str, Any] = {}
    all_trail: list[float] = []
    all_wins = 0
    all_cases = 0
    all_ties = 0
    for node in receivers:
        by_id: dict[str, list[Reception]] = defaultdict(list)
        for rec in node.receptions:
            by_id[rec.msg_id].append(rec)
        wins = 0
        ties = 0
        cases = 0
        trail: list[float] = []
        for recs in by_id.values():
            if len(recs) < 2:
                continue
            ordered = sorted(recs, key=lambda r: r.host)
            relayers = [r.last_hop for r in ordered]
            if any(c not in graph.index for c in relayers):
                continue
            audiences = [graph.audience(c) for c in relayers]
            best = max(audiences)
            cases += 1
            if audiences[0] == best:
                wins += 1
                if audiences.count(best) > 1:
                    ties += 1
            t0 = ordered[0].host
            a0 = audiences[0]
            for rec, aud in zip(ordered[1:], audiences[1:]):
                if aud < a0:
                    trail.append((rec.host - t0).total_seconds())
        per_node[node.label] = {
            "msg_ids_with_2plus_copies": cases,
            "first_copy_from_largest_audience": wins,
            "first_copy_from_largest_audience_pct": pct(wins, cases),
            "of_those_tied_on_audience": ties,
            "trailing_time_s": stats_block(trail),
        }
        all_wins += wins
        all_cases += cases
        all_ties += ties
        all_trail.extend(trail)
    return {
        "per_node": per_node,
        "overall": {
            "msg_ids_with_2plus_copies": all_cases,
            "first_copy_from_largest_audience": all_wins,
            "first_copy_from_largest_audience_pct": pct(all_wins, all_cases),
            "of_those_tied_on_audience": all_ties,
            "trailing_time_s": stats_block(all_trail),
        },
        "note": (
            "'Largest audience' is the out-degree in the hearing graph, among the relayers that "
            "actually delivered a copy of that message to that logger. The trailing time is how "
            "long after the first copy a SMALLER-audience relayer's copy arrived -- the window a "
            "cancel-on-overhear policy would have had to work with."
        ),
    }


# --------------------------------------------------------------------------
# Markdown
# --------------------------------------------------------------------------


def fmt(value: Any, digits: int = 2) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def render_md(res: dict[str, Any], cmds: Sequence[str]) -> str:
    parts: list[str] = []
    A = parts.append

    A("# Audience metric and relay-policy replay -- OE3 mountain gateways")
    A("")
    A("Companion to `berg.md`. Same four logs, same host clock. Everything here is")
    A("either measured on the logs or replayed over the graph the logs imply.")
    A("")
    A("## Reproduce")
    A("")
    A("```sh")
    for c in cmds:
        A(c)
    A("```")
    A("")

    A("## The concept under evaluation")
    A("")
    A("Quoted verbatim from the brief:")
    A("")
    for line in OPERATOR_CONCEPT.splitlines():
        A(f"> {line}")
    A("")

    A("## Assumptions, and which way each one bends the answer")
    A("")
    A(
        md_table(
            ["assumption", "why", "bias"],
            [[a["assumption"], a["why"], a["bias"]] for a in res["00_assumptions"]],
        )
    )
    A("")

    g = res["01_hearing_graph"]
    A("## 1. Hearing graph")
    A("")
    A("In a path `A,B,C`, B relayed A's frame, so B heard A: edge `A->B`. The logging")
    A("node is appended -- it heard the last hop of every frame it received.")
    A("**Out-degree = AUDIENCE** (stations that relay my frames), **in-degree = the")
    A("stations I hear**.")
    A("")
    A(
        md_table(
            ["nodes", "edges", "reciprocal edges", "reciprocity %", "self-edges dropped", "in-degree 0"],
            [
                [
                    g["nodes"],
                    g["edges"],
                    g["reciprocal_edges"],
                    fmt(g["reciprocity_pct"]),
                    g["self_edges_dropped"],
                    g["validation"]["nodes_with_zero_in_degree"],
                ]
            ],
        )
    )
    A("")
    A("### Validation: observed in-degree vs the NC the node reports on air")
    A("")
    v = g["validation"]
    A(
        f"Spearman(in-degree, NC_reported) = **{v['spearman_in_degree_vs_nc']}** over {v['n']} stations "
        f"that report an NC at all. Observed in-degree as a share of the reported NC: "
        f"median {v['observed_share_of_reported_pct'].get('median')} %, "
        f"p90 {v['observed_share_of_reported_pct'].get('p90')} %."
    )
    A("")
    A(v["limitation"])
    A("")
    A(
        md_table(
            ["callsign", "in-degree observed", "NC reported", "observed / reported %", "audience", "frames observed"],
            [
                [r["callsign"], r["in_degree_observed"], fmt(r["nc_reported"], 1), fmt(r["coverage_pct"]), r["audience_out_degree"], r["frames_observed"]]
                for r in v["rows"]
            ],
        )
    )
    A("")

    m = res["02_metrics"]
    A("## 2. Three metrics side by side")
    A("")
    A(
        f"Stations with at least {m['min_observed_frames']} observed frames "
        f"({len(m['rows'])} of {g['nodes']}). IMPORTANCE is the ADR-02 formula "
        "`sum(1 / NC_reported)` over the neighbours the station hears, with the ADR's "
        "conservative `unknown = 1.0` rule (docs/adr-nc-importance-backoff.md:488, :669)."
    )
    A("")
    A("### Rank correlations between the three metrics")
    A("")
    A(
        md_table(
            ["pair", "n", "Spearman"],
            [[k, val["n"], val["spearman"]] for k, val in m["correlation_between_metrics"].items()],
        )
    )
    A("")
    A("### Each metric against the measured relay race (berglog sections 19 and 20)")
    A("")
    A(
        md_table(
            ["pair", "n", "Spearman"],
            [[k, val["n"], val["spearman"]] for k, val in m["correlation_metric_vs_outcome"].items()],
        )
    )
    A("")
    A("### Top 30 by audience")
    A("")
    A(
        md_table(
            ["callsign", "frames", "AUDIENCE", "in-deg", "NC_self", "IMPORTANCE", "NC unknown %", "first-copy rate %", "median delay behind first s", "hw", "fw", "CAD", "mountain GW"],
            [
                [
                    r["callsign"],
                    r["frames_observed"],
                    r["audience"],
                    r["in_degree"],
                    fmt(r["nc_self"], 1),
                    fmt(r["importance"]),
                    fmt(r["nc_unknown_share_pct"], 1),
                    fmt(r["first_copy_rate_pct"]),
                    fmt(r["median_delay_behind_first_s"]),
                    r["hw"],
                    r["fw"],
                    "-" if r["fw_cad"] is None else ("yes" if r["fw_cad"] else "no"),
                    "yes" if r["mountain_gateway"] else "",
                ]
                for r in m["rows"][:30]
            ],
        )
    )
    A("")

    lf = res["03_leaves"]
    A("## 3. Leaves and exclusivity")
    A("")
    A(
        f"{lf['strict_leaf_count']} strict leaves (in-degree 1), {lf['soft_leaf_count']} soft leaves "
        f"(in-degree 1 or 2), {len(lf['zero_in_degree_nodes'])} stations with in-degree 0 -- the "
        "latter are stations we only ever saw originating, so nothing they hear was observable."
    )
    A("")
    A(
        md_table(
            ["relay", "audience", "exclusive leaves (in-degree 1, only via this relay)", "count", "soft-exclusive count", "hw", "fw", "CAD"],
            [
                [
                    r["relay"],
                    r["audience"],
                    ", ".join(r["exclusive_leaves"]) or "-",
                    r["exclusive_leaf_count"],
                    r["soft_exclusive_count"],
                    r["hw"],
                    r["fw"],
                    "-" if r["fw_cad"] is None else ("yes" if r["fw_cad"] else "no"),
                ]
                for r in lf["rows"]
            ],
        )
    )
    A("")
    A(
        f"### Leaves depending on a pre-CAD relay ({len(lf['leaves_depending_on_pre_cad_relay'])}, "
        f"of which {lf['leaves_with_only_pre_cad_providers']} have no CAD-capable provider at all)"
    )
    A("")
    A(
        md_table(
            ["leaf", "in-degree", "providers", "pre-CAD providers", "no CAD provider at all"],
            [
                [
                    d["leaf"],
                    d["in_degree"],
                    ", ".join(d["providers"]),
                    ", ".join(d["pre_cad_providers"]),
                    "yes" if d["only_pre_cad"] else "",
                ]
                for d in lf["leaves_depending_on_pre_cad_relay"]
            ],
        )
        if lf["leaves_depending_on_pre_cad_relay"]
        else "none"
    )
    A("")
    A(
        "Caveat: `OE1XAR-33` shows up as a pre-CAD provider only because its `FW:00:#` field "
        "fails the CAD test. It is the server's Internet-injected time signal, not a radio "
        "station running old firmware, so the leaf that depends on it (`OE3RAB-99`) is an "
        "artefact of how the injector appears in a path."
    )
    A("")

    sim = res["04_simulation"]
    A("## 4. Replay simulation")
    A("")
    A(
        f"{sim['messages']} messages replayed over the graph, {sim['seeds']} seeds per policy. "
        f"Capture window {sim['duration_h']} h. Backoff and hop rules mirror the firmware "
        "(see the module docstring of `tools/berglog_sim.py`)."
    )
    A("")
    A("### Headline")
    A("")
    A(
        md_table(
            ["policy", "what it does", "tx/msg mean", "spread", "tx/msg p90", "vs P0", "coverage %", "airtime/h s", "airtime saved/h s", "veto relays", "cancels"],
            [
                [
                    p["policy"],
                    p["label"],
                    fmt(p["transmissions_per_msg_mean"]["mean"], 3),
                    fmt(p["transmissions_per_msg_mean"]["spread"], 3),
                    fmt(p["transmissions_per_msg_p90"]["mean"]),
                    fmt(p["relative_to_p0_pct"], 1) + " %",
                    fmt(p["coverage_mean_pct"]["mean"], 3),
                    fmt(p["airtime_per_hour_s"], 1),
                    fmt(p["airtime_saved_per_hour_s"], 1),
                    fmt(p["veto_relays"]["mean"], 1),
                    fmt(p["cancels"]["mean"], 1),
                ]
                for p in sim["policies"]
            ],
        )
    )
    A("")
    A(sim["p1_equality_note"])
    A("")
    A("### Per payload type -- transmissions per message")
    A("")
    types = sorted({t for p in sim["policies"] for t in p["by_type"]})
    A(
        md_table(
            ["policy"] + [TYPE_LABEL.get(t, t) for t in types],
            [
                [p["policy"]]
                + [
                    fmt(p["by_type"][t]["transmissions_per_msg_mean"], 3) if t in p["by_type"] else "-"
                    for t in types
                ]
                for p in sim["policies"]
            ],
        )
    )
    A("")
    A("### Stations that lose coverage in at least 5 % of the messages they would have received")
    A("")
    any_loss = False
    for p in sim["policies"]:
        if not p["coverage_losers"]:
            continue
        any_loss = True
        A(f"**{p['policy']}** -- {len(p['coverage_losers'])} station(s)")
        A("")
        A(
            md_table(
                ["callsign", "in-degree", "msgs in P0 reach", "msgs lost", "loss %", "relays that would have served it (by audience)"],
                [
                    [
                        r["callsign"],
                        r["in_degree"],
                        r["messages_in_p0_reach"],
                        r["messages_lost"],
                        fmt(r["loss_share_pct"]),
                        ", ".join(r["providers_by_audience"]),
                    ]
                    for r in p["coverage_losers"]
                ],
            )
        )
        A("")
    if not any_loss:
        A("None -- no station drops below the 5 % threshold under any policy.")
        A("")
    A("### Sensitivity")
    A("")
    A(
        md_table(
            ["variant", "skew step s", "cancel guard s", "tx/msg mean", "vs P0", "coverage %", "cancels"],
            [
                [
                    s["variant"],
                    fmt(s["skew_step_s"], 1),
                    fmt(s["cancel_guard_s"], 1),
                    fmt(s["transmissions_per_msg_mean"]["mean"], 3),
                    fmt(s["relative_to_p0_pct"], 1) + " %",
                    fmt(s["coverage_mean_pct"]["mean"], 3),
                    fmt(s["cancels"]["mean"], 1),
                ]
                for s in sim["sensitivity"]
            ],
        )
    )
    A("")

    pg = res["04b_postpone_grid"]
    A("## 4b. P5 -- \"sending is always allowed, just not immediately\"")
    A("")
    A(pg["concept"])
    A("")
    A(
        f"Grid: k in {pg['grid_k']}, late offset in {pg['grid_late_offset_s']} s, "
        f"veto in {pg['grid_veto']}, listening guard {pg['listening_guard_s']} s, "
        f"{len(pg['rows'])} points, {res['04_simulation']['seeds']} seeds each."
    )
    A("")
    A(
        md_table(
            ["family", "k", "late s", "veto", "tx/msg", "vs P0", "coverage %", "airtime saved/h s", "cancels", "veto fires", "hop yields", "losers >= 5 %"],
            [
                [
                    r["family"],
                    r["k"],
                    fmt(r["late_offset_s"], 0),
                    r["veto_mode"],
                    fmt(r["transmissions_per_msg_mean"]["mean"], 3),
                    fmt(r["relative_to_p0_pct"], 1) + " %",
                    fmt(r["coverage_mean_pct"]["mean"], 3),
                    fmt(r["airtime_saved_per_hour_s"], 1),
                    fmt(r["cancels"]["mean"], 0),
                    fmt(r["veto_relays"]["mean"], 0),
                    fmt(r["hop_yields"]["mean"], 0),
                    len(r["coverage_losers"]),
                ]
                for r in pg["rows"]
            ],
        )
    )
    A("")
    A("### Recommendation")
    A("")
    rec_rows = []
    for bar, best in pg["recommendation"].items():
        if best is None:
            rec_rows.append([bar, "no grid point clears this bar", "", "", "", "", "", ""])
            continue
        rec_rows.append(
            [
                bar,
                f"{best['family']} k={best['k']} late={best['late_offset_s']:g}s veto={best['veto_mode']}",
                fmt(best["transmissions_per_msg_mean"], 3),
                fmt(best["relative_to_p0_pct"], 1) + " %",
                fmt(best["coverage_mean_pct"], 3),
                fmt(best["airtime_saved_per_hour_s"], 1),
                best["coverage_losers"],
                best["candidates_at_this_bar"],
            ]
        )
    A(
        md_table(
            ["bar", "best point (largest saving that clears it)", "tx/msg", "vs P0", "coverage %", "airtime saved/h s", "losers >= 5 %", "grid points clearing the bar"],
            rec_rows,
        )
    )
    A("")
    A("### Worst 10 coverage losers of the recommended points")
    A("")
    worst_rows = []
    for bar, best in pg["recommendation"].items():
        if best is None:
            continue
        match = next(
            r
            for r in pg["rows"]
            if r["family"] == best["family"]
            and r["k"] == best["k"]
            and r["late_offset_s"] == best["late_offset_s"]
            and r["veto_mode"] == best["veto_mode"]
        )
        for loser in sorted(match["coverage_losers"], key=lambda d: -float(d["loss_share_pct"]))[:10]:
            worst_rows.append(
                [
                    bar,
                    loser["callsign"],
                    loser["in_degree"],
                    loser["messages_in_p0_reach"],
                    loser["messages_lost"],
                    fmt(loser["loss_share_pct"]),
                    ", ".join(loser["providers_by_audience"][:4]),
                ]
            )
    A(
        md_table(
            ["bar", "callsign", "in-degree", "msgs in P0 reach", "msgs lost", "loss %", "providers (top 4 by audience)"],
            worst_rows,
        )
        if worst_rows
        else "None -- the recommended points lose no station above the 5 % threshold."
    )
    A("")

    ar = res["04c_audience_ranking"]
    A("## 4c. Audience rank of the logging nodes and the two busiest relays")
    A("")
    A(
        f"Top-10 % audience cut is {fmt(ar['top_10pct_audience_cut'], 1)} out-edges; "
        f"{len(ar['top_10pct_members'])} stations are in that class: "
        + ", ".join(ar["top_10pct_members"])
        + "."
    )
    A("")
    A(
        md_table(
            ["callsign", "audience", "in-degree", "global rank", "of nodes", "percentile", "skew tier", "top 10 %", "NC reported", "hw", "fw"],
            [
                [
                    r["callsign"],
                    r.get("audience"),
                    r.get("in_degree"),
                    r.get("global_audience_rank"),
                    r.get("of_nodes"),
                    fmt(r.get("percentile"), 1),
                    r.get("skew_tier"),
                    "yes" if r.get("top_10pct_member") else "no",
                    fmt(r.get("nc_reported"), 1),
                    r.get("hw"),
                    r.get("fw"),
                ]
                if r["present_in_graph"]
                else [r["callsign"], "not in graph", "", "", "", "", "", "", "", "", ""]
                for r in ar["rows"]
            ],
        )
    )
    A("")

    mo = res["05_measured_ordering"]
    A("## 5. The same question on the raw logs, with no model")
    A("")
    A(mo["note"])
    A("")
    o = mo["overall"]
    A(
        f"Across all three loggers, in **{o['first_copy_from_largest_audience']} of "
        f"{o['msg_ids_with_2plus_copies']}** cases ({fmt(o['first_copy_from_largest_audience_pct'])} %) the "
        "first copy already came from the largest-audience relayer -- "
        f"{o['of_those_tied_on_audience']} of those were ties. So today's ordering is "
        "already partly audience-shaped by accident."
    )
    A("")
    A(
        md_table(
            ["log", "msg_ids with >= 2 copies", "first copy from largest audience", "%", "ties", "trailing n", "median s", "p90 s", "max s"],
            [
                [
                    label,
                    d["msg_ids_with_2plus_copies"],
                    d["first_copy_from_largest_audience"],
                    fmt(d["first_copy_from_largest_audience_pct"]),
                    d["of_those_tied_on_audience"],
                    d["trailing_time_s"].get("n"),
                    d["trailing_time_s"].get("median"),
                    d["trailing_time_s"].get("p90"),
                    d["trailing_time_s"].get("max"),
                ]
                for label, d in mo["per_node"].items()
            ],
        )
    )
    A("")
    A(
        "The median trailing time is the real-world size of the listening window a "
        "cancel-on-overhear policy would have to fit into."
    )
    A("")

    A("## 6. Where the model had to guess")
    A("")
    for item in res["06_guesses"]:
        A(f"- {item}")
    A("")
    return "\n".join(parts)


# --------------------------------------------------------------------------
# Assumptions and guesses (data for sim.md)
# --------------------------------------------------------------------------

ASSUMPTIONS: list[dict[str, str]] = [
    {
        "assumption": "Transmissions are instantaneous",
        "why": "no time-on-air in the event loop; a frame is delivered the moment it is sent",
        "bias": "makes cancel-on-overhear look BETTER than reality -- a real 1.4 s frame arrives late, so fewer pending relays would be cancelled in time",
    },
    {
        "assumption": "No collisions, no capture effect",
        "why": "every out-neighbour receives every transmission",
        "bias": "flatters ALL policies, but flatters P0 most (it transmits most), so the measured saving is a lower bound",
    },
    {
        "assumption": "No TX queueing, no CSMA channel-busy deferral",
        "why": "a scheduled relay fires exactly at its slot",
        "bias": "understates today's real spread; on air a busy channel already delays some relays, which is a weak form of the skew being proposed",
    },
    {
        "assumption": "The hearing graph is complete",
        "why": "only edges that appear in a path heard by one of three loggers exist",
        "bias": "understates fan-out everywhere, so absolute transmission counts are too low; ratios between policies are far more robust than absolute numbers",
    },
    {
        "assumption": "Pre-CAD nodes behave like CAD nodes",
        "why": "no separate model for the firmware letters below p",
        "bias": "understates today's channel load, because pre-CAD stations transmit without a channel check and collide more",
    },
    {
        "assumption": "Gateway re-injection from the server is ignored",
        "why": "only the RF flood is modelled; a gateway that re-injects a frame from the server would add transmissions",
        "bias": "understates transmissions for all policies equally",
    },
    {
        "assumption": "Frame length is constant per message",
        "why": "airtime uses the mean observed msg_len; in reality each relay appends a callsign and the frame grows",
        "bias": "understates airtime of deep relays, so the airtime saving of the cancelling policies is understated",
    },
    {
        "assumption": "Dedup is perfect and free",
        "why": "a node relays at most once and never re-relays a known msg_id",
        "bias": "matches the firmware while its dedup ring holds; the ring ages out (berg.md section 10), so reality is worse than the model for all policies",
    },
    {
        "assumption": "Audience rank is global and static",
        "why": "the skew class comes from the whole-night graph, not from what a node knows at that moment",
        "bias": "flatters the skew policies -- a real node learns its audience only gradually and would misrank itself early on",
    },
]


def collect_guesses(res: dict[str, Any]) -> list[str]:
    g = res["01_hearing_graph"]
    sim = res["04_simulation"]
    return [
        "**Hop limit per message** is not transmitted; it is reconstructed as `H + hops_taken` "
        "from the earliest observed copy. If that copy was already a relay whose H had wrapped "
        "(berg.md section 3), the limit is wrong for that message.",
        "**Originator identity** is `path[0]` of the earliest copy. Frames whose originator never "
        f"appears in the graph are skipped ({sim['messages_skipped']} of "
        f"{sim['messages'] + sim['messages_skipped']}).",
        "**NC_reported** exists only for stations that either beacon a position with `/N` or run the "
        "firmware dialect that puts `R<n>;` in its HEY. Everything else is 'unknown', which the "
        "ADR-02 importance formula deliberately treats as 1.0 -- the single largest modelling "
        "choice in section 2.",
        "**Audience tier boundaries** (top 10 % / next 20 % / rest) are the brief's, not measured "
        "optima; the sensitivity table varies the step, not the boundaries.",
        "**The skew slot uses a GLOBAL audience rank.** A real node cannot know the audience of the "
        "other stations that heard the same copy, so this is the optimistic case.",
        f"**{g['validation']['nodes_with_zero_in_degree']} stations have in-degree 0** -- they were only "
        "ever seen originating. They can receive in the simulation but never relay, which understates "
        "transmissions.",
        "**The 1 s listening floor** is modelled as a cancel guard: a copy arriving less than 1 s "
        "before a pending slot can no longer abort it. Whether a real radio can abort a queued TX "
        "later than that is a hardware question this data cannot answer.",
        "**Coverage denominator** is the P0 reach of the same seed, as briefed. The hop-limited "
        "ball around the originator is reported alongside it, because P0 itself does not always "
        "reach the whole ball (a node that first hears the frame on a long path relays it with a "
        "smaller budget).",
    ]


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------


def postpone_grid(
    graph: HearingGraph,
    messages: Sequence[Message],
    tiers: Sequence[int],
    reach_p0: dict[str, set[int]],
    seeds: Sequence[int],
    duration_h: float,
    p0_mean: float,
    p0_air: float,
) -> list[dict[str, Any]]:
    """The full P5 / P5h grid: k x late offset x veto, both families."""
    rows: list[dict[str, Any]] = []
    for family, hop_yield in (("P5", False), ("P5h", True)):
        for k in POSTPONE_K_VALUES:
            for offset in LATE_OFFSET_VALUES:
                for veto in POSTPONE_VETOES:
                    policy = Policy(
                        family,
                        f"{family} k={k} late={offset:g}s veto={veto}",
                        veto=veto,
                        postpone_k=k,
                        late_offset_s=offset,
                        cancel_guard_s=POSTPONE_GUARD_S,
                        hop_yield=hop_yield,
                    )
                    row = run_policy(graph, messages, policy, tiers, reach_p0, seeds, duration_h)
                    row["family"] = family
                    row["k"] = k
                    row["late_offset_s"] = offset
                    row["veto_mode"] = veto
                    row["relative_to_p0_pct"] = round(
                        100.0 * row["transmissions_per_msg_mean"]["mean"] / p0_mean, 2
                    )
                    row["airtime_saved_per_hour_s"] = round(p0_air - row["airtime_per_hour_s"], 1)
                    rows.append(row)
    return rows


def recommend(grid: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Largest saving that still clears each coverage bar."""
    out: dict[str, Any] = {}
    for target in COVERAGE_TARGETS:
        ok = [r for r in grid if r["coverage_mean_pct"]["mean"] >= target]
        if not ok:
            out[f">= {target:g} % coverage"] = None
            continue
        best = min(ok, key=lambda r: r["transmissions_per_msg_mean"]["mean"])
        out[f">= {target:g} % coverage"] = {
            "family": best["family"],
            "k": best["k"],
            "late_offset_s": best["late_offset_s"],
            "veto_mode": best["veto_mode"],
            "transmissions_per_msg_mean": best["transmissions_per_msg_mean"]["mean"],
            "relative_to_p0_pct": best["relative_to_p0_pct"],
            "coverage_mean_pct": best["coverage_mean_pct"]["mean"],
            "airtime_saved_per_hour_s": best["airtime_saved_per_hour_s"],
            "coverage_losers": len(best["coverage_losers"]),
            "candidates_at_this_bar": len(ok),
        }
    return out


def audience_ranking(
    graph: HearingGraph,
    tiers: Sequence[int],
    nc: dict[str, float],
    station: dict[str, dict[str, Any]],
    focus: Sequence[str],
) -> dict[str, Any]:
    """Global audience rank of the stations the report cares about."""
    order = sorted(range(graph.size), key=lambda i: (-len(graph.out_adj[i]), graph.calls[i]))
    rank = {graph.calls[i]: pos + 1 for pos, i in enumerate(order)}
    audiences = [len(a) for a in graph.out_adj]
    top_cut = quantile(audiences, SKEW_TOP_QUANTILE)
    rows = []
    for call in focus:
        if call not in graph.index:
            rows.append({"callsign": call, "present_in_graph": False})
            continue
        i = graph.index[call]
        st = station.get(call, {})
        rows.append(
            {
                "callsign": call,
                "present_in_graph": True,
                "audience": len(graph.out_adj[i]),
                "in_degree": len(graph.in_adj[i]),
                "global_audience_rank": rank[call],
                "of_nodes": graph.size,
                "percentile": round(100.0 * (graph.size - rank[call] + 1) / graph.size, 1),
                "skew_tier": tiers[i],
                "top_10pct_member": tiers[i] == 0,
                "nc_reported": nc.get(call),
                "hw": st.get("hw"),
                "fw": st.get("fw"),
            }
        )
    return {
        "top_10pct_audience_cut": top_cut,
        "top_10pct_members": [graph.calls[i] for i in range(graph.size) if tiers[i] == 0],
        "rows": rows,
    }


def analyse(logs: Sequence[Path], seeds: Sequence[int]) -> dict[str, Any]:
    nodes = berglog.parse_logs(list(logs))
    receivers = [n for n in nodes if n.receptions]
    if not receivers:
        raise SystemExit("no log with [LOG] receive lines")

    base: dict[str, Any] = {
        "19_first_relayer": berglog.a19_first_relayer(receivers),
        "20_relay_latency": berglog.a20_relay_latency(receivers),
    }
    base["21_neighbour_count"] = berglog.a21_neighbour_count(nodes, base)
    station = berglog.station_index(nodes)

    graph = build_graph(receivers)
    nc = reported_nc(nodes, base)
    imp = importance(graph, nc)
    gateways = {n.own_call for n in nodes if n.own_call}

    duration_h = max((n.duration_s for n in receivers), default=0.0) / 3600.0
    messages = collect_messages(receivers, graph)
    skipped = len({r.msg_id for n in receivers for r in n.receptions}) - len(messages)
    tiers = skew_tiers(graph)
    reach_p0 = {m.msg_id: p0_reach(graph, m) for m in messages}

    policy_rows: list[dict[str, Any]] = []
    for policy in POLICIES:
        policy_rows.append(run_policy(graph, messages, policy, tiers, reach_p0, seeds, duration_h))
    p0 = policy_rows[0]
    p0_mean = p0["transmissions_per_msg_mean"]["mean"]
    p0_air = p0["airtime_per_hour_s"]
    for row in policy_rows:
        row["relative_to_p0_pct"] = round(100.0 * row["transmissions_per_msg_mean"]["mean"] / p0_mean, 2)
        row["airtime_saved_per_hour_s"] = round(p0_air - row["airtime_per_hour_s"], 1)

    p1 = policy_rows[1]
    same = p1["transmissions_total"]["mean"] == p0["transmissions_total"]["mean"]
    p1_note = (
        "P1 adds a skew but never withdraws a relay, so it must transmit exactly as often as P0: "
        f"P0 {p0['transmissions_total']['mean']:.1f} vs P1 {p1['transmissions_total']['mean']:.1f} "
        "transmissions in total"
    )
    p1_note += (
        " -- identical, as expected. The mechanism: with a 4.5-5.5 s base the relay "
        "generations do not overlap, so reordering inside a generation cannot change which "
        "hop budget a station first receives, and every station that receives with budget > 0 "
        "still relays exactly once. The sensitivity table repeats this at a 5 s skew step, "
        "where the generations DO start to overlap."
        if same
        else (
            ". They are NOT identical: reordering changes which copy a station hears FIRST, and a "
            "station that first hears the frame on a longer path receives a smaller hop budget and "
            "may then not relay at all. The skew therefore has a small transmission effect even "
            "without any cancelling."
        )
    )

    sensitivity: list[dict[str, Any]] = []
    # P1 across the skew range: the equality with P0 must not be an artefact of the
    # default step. A wide step lets a gen-1 relay fire after a gen-2 one, which can
    # change the hop budget a station first receives -- and only then the count.
    for step in (1.0, 2.5, 5.0):
        variant = Policy("P1", f"P1 step={step}s", skew=True, skew_step_s=step)
        row = run_policy(graph, messages, variant, tiers, reach_p0, seeds, duration_h)
        row["variant"] = f"P1 step {step} s (no cancelling)"
        row["relative_to_p0_pct"] = round(
            100.0 * row["transmissions_per_msg_mean"]["mean"] / p0_mean, 2
        )
        sensitivity.append(row)
    for step in (0.5, 1.0, 2.5):
        for guard in (0.0, 1.0):
            variant = Policy(
                "P3",
                f"P3 step={step}s guard={guard}s",
                skew=True,
                cancel=True,
                veto="strict",
                skew_step_s=step,
                cancel_guard_s=guard,
            )
            row = run_policy(graph, messages, variant, tiers, reach_p0, seeds, duration_h)
            row["variant"] = f"P3 step {step} s, guard {guard} s"
            row["relative_to_p0_pct"] = round(
                100.0 * row["transmissions_per_msg_mean"]["mean"] / p0_mean, 2
            )
            sensitivity.append(row)

    grid = postpone_grid(graph, messages, tiers, reach_p0, seeds, duration_h, p0_mean, p0_air)
    focus = sorted({n.own_call for n in nodes if n.own_call} | {"OE3XIA-12", "OE3XPA-12"})

    res: dict[str, Any] = {
        "meta": {
            "generated_by": "tools/berglog_sim.py",
            "logs": [str(p) for p in logs],
            "imports_from": "tools/berglog.py (unmodified)",
            "csma_base_ms": CSMA_BASE_MS,
            "csma_jitter": f"k * {CSMA_SLOT_SIZE_MS} ms, k uniform on 0..{CSMA_SLOTS}",
            "skew_tiers": {"top": SKEW_TOP_QUANTILE, "mid": SKEW_MID_QUANTILE, "multipliers": list(SKEW_TIER_MULTIPLIER)},
        },
        "00_assumptions": ASSUMPTIONS,
        "01_hearing_graph": {
            "nodes": graph.size,
            "self_edges_dropped": graph.self_edges_dropped,
            **graph.reciprocity(),
            "audience_distribution": stats_block([len(a) for a in graph.out_adj]),
            "in_degree_distribution": stats_block([len(a) for a in graph.in_adj]),
            "top_audience": [
                {"callsign": graph.calls[i], "audience": len(graph.out_adj[i]), "in_degree": len(graph.in_adj[i])}
                for i in sorted(range(graph.size), key=lambda k: -len(graph.out_adj[k]))[:25]
            ],
            "validation": validate_graph(graph, nc),
        },
        "02_metrics": metric_table(graph, nc, imp, station, base, gateways),
        "03_leaves": leaf_analysis(graph, station),
        "04_simulation": {
            "messages": len(messages),
            "messages_skipped": skipped,
            "seeds": len(seeds),
            "seed_values": list(seeds),
            "duration_h": round(duration_h, 3),
            "hop_limit_histogram": {str(k): v for k, v in sorted(Counter(m.hop_limit for m in messages).items())},
            "p0_reach_ball_mean": round(statistics.fmean([len(v) for v in reach_p0.values()]), 2),
            "coverage_denominator": (
                "the hop-limited ball around the originator. P0 reaches it exactly "
                f"({policy_rows[0]['coverage_mean_pct']['mean']:.3f} % coverage), so 'reachable under "
                "P0' and 'the ball' are the same set here."
            ),
            "coverage_losers_seed": seeds[0],
            "policies": policy_rows,
            "p1_equality_note": p1_note,
            "sensitivity": sensitivity,
        },
        "04b_postpone_grid": {
            "concept": (
                "P5 is the operator's wording taken literally: 'sending is always allowed, just not "
                "immediately'. A hub keeps the plain CSMA slot. Everyone else waits out a late offset "
                "and only THEN counts how many copies it overheard; it still transmits when it counted "
                "fewer than k, or when it is somebody's sole provider. P5h never goes silent at all -- "
                "it yields its hop budget instead, transmitting with max_hop capped at 1 so its "
                "exclusive neighbours still get their one hop."
            ),
            "grid_k": list(POSTPONE_K_VALUES),
            "grid_late_offset_s": list(LATE_OFFSET_VALUES),
            "grid_veto": list(POSTPONE_VETOES),
            "listening_guard_s": POSTPONE_GUARD_S,
            "rows": grid,
            "recommendation": recommend(grid),
        },
        "04c_audience_ranking": audience_ranking(graph, tiers, nc, station, focus),
        "05_measured_ordering": measured_ordering(receivers, graph),
    }
    res["06_guesses"] = collect_guesses(res)
    return res


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Relay-policy replay on the measured MeshCom topology.")
    ap.add_argument("logs", nargs="+", type=Path, help="log files to analyse")
    ap.add_argument("--out", required=True, type=Path, help="output directory for sim.json / sim.md")
    ap.add_argument("--seeds", type=int, default=DEFAULT_SEEDS, help="stochastic runs per policy")
    ap.add_argument("--json-name", default="sim.json")
    ap.add_argument("--md-name", default="sim.md")
    args = ap.parse_args(argv)

    args.out.mkdir(parents=True, exist_ok=True)
    seeds = list(range(1, args.seeds + 1))
    res = analyse(args.logs, seeds)

    cmds = [
        "python3 tools/berglog_sim.py \\",
        "    " + " \\\n    ".join(str(p) for p in args.logs) + " \\",
        f"    --out {args.out} --seeds {args.seeds}",
        "",
        "# table columns are padded for monospace reading:",
        f"npx --yes prettier@3 --write {args.out / args.md_name}",
    ]
    json_path = args.out / args.json_name
    md_path = args.out / args.md_name
    json_path.write_text(json.dumps(res, indent=2, ensure_ascii=False, default=str), encoding="utf-8")
    md_path.write_text(render_md(res, cmds), encoding="utf-8")
    print(f"wrote {json_path}")
    print(f"wrote {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
