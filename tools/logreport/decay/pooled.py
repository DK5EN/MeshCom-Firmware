"""Pooled headroom / CET figures appended to the merged decay results (after merge_results.run)."""
from __future__ import annotations

from typing import Any

from .common import NODES


def append_pooled(R: dict[str, Any], annotations: dict[str, Any]) -> None:
    """Add network.headroom_pooled / cet / examples to R in place.

    Hand-written judgement comes from --annotations: `cet_manual` (counts that were verified by
    grep, merged into network.cet) and `examples` (named log excerpts). Without it only measured
    values are written.
    """
    hp: dict[str, Any] = {}
    for p in ("p1", "p3", "p4", "p5"):
        n = sum(R["nodes"][x]["headroom"][p]["n_sent"] for x in NODES)
        d = sum(R["nodes"][x]["headroom"][p]["n_dropped"] for x in NODES)
        ge = sum(R["nodes"][x]["headroom"][p]["n_ge80pct_ttl_sent"] for x in NODES)
        ge50 = sum(R["nodes"][x]["headroom"][p]["n_ge50pct_ttl_sent"] for x in NODES)
        mx = max((R["nodes"][x]["headroom"][p]["max"] for x in NODES if R["nodes"][x]["headroom"][p]["max"] is not None), default=None)
        hp[p] = {"ttl_s": R["nodes"][NODES[0]]["headroom"][p]["ttl_s"], "n_sent": n, "n_dropped": d,
                 "n_sent_ge80pct_ttl": ge, "pct_sent_ge80": round(100 * ge / n, 2) if n else 0.0,
                 "n_sent_ge50pct_ttl": ge50, "max_sent_lat_s": mx,
                 "pct_reached_ttl": round(100 * d / (n + d), 2) if n + d else 0.0}
    R["network"]["headroom_pooled"] = hp
    ages = [R["nodes"][x].get("cet_tx_age_s") or {} for x in NODES]
    cet: dict[str, Any] = {
        "cet_tx_n": sum(R["nodes"][x]["cet_tx_n"] for x in NODES),
        "max_payload_age_at_tx_s": max((a["max"] for a in ages if a.get("max") is not None), default=None),
        "n_tx_age_gt_120s": sum(a.get("n_gt_120", 0) for a in ages)}
    cet.update(annotations.get("cet_manual", {}))
    R["network"]["cet"] = cet
    R["network"]["examples"] = annotations.get("examples", {})
