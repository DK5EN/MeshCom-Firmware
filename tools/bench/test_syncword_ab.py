"""Pure logic of tools/bench/syncword_ab.py (no hardware)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import syncword_ab as sw  # noqa: E402

TX = "[LOG] TX x0000000{n} : H02 prio=5 src=o wait=10 q=1 cad=0 len=54 t=0"
RX = "[LOG] 029 : x0000000{n} H02 S0 T0 M01 {path}>{{9999}}SWAB; HW:43 RSSI:{r} SNR:{s} DUP:n OWN:- t=1"


def test_self_test():
    assert sw.self_test() == 0


def test_only_own_ids_in_window_count():
    logs = {
        "DK5EN-90": [(10.0, TX.format(n=1)), (11.0, TX.format(n=2)), (99.0, TX.format(n=3))],
        "DK5EN-1": [
            (10.5, RX.format(n=1, path="DK5EN-90", r=-60, s=7)),
            (11.5, RX.format(n=2, path="DK5EN-90", r=-70, s=3)),
            (12.0, RX.format(n=2, path="DK5EN-90,DK5EN-98", r=-50, s=9)),  # relay copy, ignored
            (12.5, RX.format(n=7, path="DK5EN-92", r=-40, s=10)),  # other originator
        ],
    }
    (row,) = [r for r in sw.pair_table(logs, {"DK5EN-90": (9.0, 20.0)}) if r["receiver"] == "DK5EN-1"]
    assert row["sent"] == 2  # the id at t=99 is outside the window
    assert row["direct"] == 2 and row["relay_only"] == 0
    assert row["rssi_med"] == -65 and row["rssi_min"] == -70
    assert row["snr_min"] == 3


def test_relay_only_and_errors_counted_inside_window():
    logs = {
        "DK5EN-1": [(10.0, TX.format(n=1))],
        "DK5EN-90": [
            (10.5, RX.format(n=1, path="DK5EN-1,DK5EN-98", r=-80, s=-2)),
            (11.0, "[LOG] ERR rssi=-118 snr=-15 len=0 ferr=0 t=5"),
            (40.0, "[LOG] ERR rssi=-118 snr=-15 len=0 ferr=0 t=6"),  # after the window
        ],
    }
    (row,) = sw.pair_table(logs, {"DK5EN-1": (9.0, 20.0)})
    assert (row["direct"], row["relay_only"], row["err_in_window"]) == (0, 1, 1)
    assert row["rssi_med"] is None


def test_render_has_a_row_per_pair():
    rows = sw.pair_table({"A-1": [(1.0, TX.format(n=1))], "B-2": []}, {"A-1": (0.0, 5.0)})
    md = sw.render(rows, "B")
    assert "| A-1 | B-2 | 1 | 0 |" in md
