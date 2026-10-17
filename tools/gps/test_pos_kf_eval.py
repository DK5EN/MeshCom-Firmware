"""Tests for pos_kf_eval.py with synthetic capture lines."""

from __future__ import annotations

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pos_kf_eval as ev  # noqa: E402


def pos_line(t: str, raw: tuple[float, float], kf: tuple[float, float], hdop: float = 1.2) -> str:
    return "%s  [GPS ]...pos raw:%.6f,%.6f kf:%.6f,%.6f hdop:%.1f sig:3.5m rej:0" % (
        t,
        raw[0],
        raw[1],
        kf[0],
        kf[1],
        hdop,
    )


def test_parse_pos_and_shot_lines() -> None:
    lines = [
        "2026-10-17 10:00:00.250  [GPS ]...pos raw:48.123456,11.567890 kf:48.123400,11.567800"
        " hdop:1.4 sig:4.2m rej:1",
        "2026-10-17 10:00:03.250  10:00:03 [POSINFO]...one-shot set - distance > 50m (61.2)",
        "2026-10-17 10:00:04.000  unrelated",
        "garbage without timestamp [GPS ]...pos raw:1,2 kf:1,2 hdop:1 sig:1m rej:0",
    ]
    samples, shots = ev.parse_lines(lines)
    assert len(samples) == 1 and len(shots) == 1
    s = samples[0]
    assert s.raw == (48.123456, 11.56789) and s.kf == (48.1234, 11.5678)
    assert s.hdop == 1.4 and s.sig == 4.2 and s.rej == 1
    assert math.isclose(shots[0] - s.t, 3.0)


def test_parse_w2_debug_line() -> None:
    line = (
        "2026-10-17 10:00:00.250  [GPS ]pos raw:48.123456,11.567890 kf:48.123400,11.567800"
        " held:48.123300,11.567700 hdop:1.4 sig:4.2 rej:1 spd:0.35 crs:181.5 st:still nm:2"
    )
    samples, _ = ev.parse_lines([line])
    assert len(samples) == 1
    s = samples[0]
    assert s.held == (48.1233, 11.5677)
    assert s.spd == 0.35 and s.crs == 181.5 and s.st == "still" and s.nm == 2
    assert s.sig == 4.2 and s.rej == 1
    res = ev.evaluate(samples, [], False)
    assert "g1_held" in res
    # old format has no extras
    old, _ = ev.parse_lines([pos_line("2026-10-17 10:00:00.250", (1.0, 2.0), (1.0, 2.0))])
    assert old[0].held is None and old[0].nm is None


def test_aprs_cell_northeast() -> None:
    # 48.1234: 48 deg + 0.1234*60 = 7.404 min -> 4807.40N
    # 11.5678: 11 deg + 0.5678*60 = 34.068 min -> 01134.07E
    assert ev.aprs_cell(48.1234, 11.5678) == ("4807.40N", "01134.07E")


def test_aprs_cell_south_west() -> None:
    # -33.8688: 33 deg 52.128 min -> 3352.13S ; -122.4194: 122 deg 25.164 -> 12225.16W
    assert ev.aprs_cell(-33.8688, -122.4194) == ("3352.13S", "12225.16W")


def test_aprs_cell_border_and_cround4() -> None:
    # 0.0083 deg = 0.498 min -> rounds up to .50 ; 0.0082 deg = 0.492 min -> .49
    assert ev.aprs_cell(48.0083, 11.0)[0] == "4800.50N"
    assert ev.aprs_cell(48.0082, 11.0)[0] == "4800.49N"
    # cround4abs cuts to 1e-4 first: 48.00834 -> 48.0083 (not 48.00834)
    assert ev.aprs_cell(48.00834, 11.0)[0] == "4800.50N"
    assert ev.aprs_cell(48.00826, 11.0)[0] == "4800.50N"  # rounds to 48.0083


def test_g1_stats_constructed_series() -> None:
    a = (48.1234, 11.5678)
    b = (48.0083, 11.5678)
    pts = [a] * 9 + [b]
    st = ev.series_stats(pts)
    assert st["n"] == 10 and st["distinct_cells"] == 2
    assert math.isclose(st["modal_share"], 0.9)
    assert st["cell_changes"] == 1
    assert st["modal_cell"] == {"lat": "4807.40N", "lon": "01134.07E"}
    # median is a; the one outlier is ~12.7 km away -> p95 dominated by it
    assert st["p95_dist_to_median_m"] > 1000.0
    flat = ev.series_stats([a] * 5)
    assert flat["modal_share"] == 1.0 and flat["std_m"] == 0.0 and flat["cell_changes"] == 0


def test_track_lag_constant_speed() -> None:
    # 14 m/s north, 1 Hz, kf trails raw by 10 m along track
    m_per_deg = ev.EARTH_M_PER_DEG
    lines = []
    for i in range(90):
        lat = 48.0 + 14.0 * i / m_per_deg
        klat = 48.0 + (14.0 * i - 10.0) / m_per_deg
        sec = i
        lines.append(
            pos_line("2026-10-17 10:%02d:%02d.000" % (sec // 60, sec % 60), (lat, 11.0), (klat, 11.0))
        )
    samples, _ = ev.parse_lines(lines)
    res = ev.track_lag(samples)
    assert res["segments"] == 1 and res["n"] > 0
    assert math.isclose(res["median_signed_m"], -10.0, abs_tol=0.5)
    assert math.isclose(res["p95_abs_m"], 10.0, abs_tol=0.5)


def test_g4_stationary_shot_count() -> None:
    lines = [
        pos_line("2026-10-17 10:00:%02d.000" % i, (48.1234, 11.5678), (48.1234, 11.5678))
        for i in range(0, 60, 3)
    ]
    lines.append("2026-10-17 10:00:30.500  [POSINFO]...one-shot set - distance > 50m (61.2)")
    samples, shots = ev.parse_lines(lines)
    g4 = ev.stationary_shots(samples, shots)
    assert g4 == {"shots_total": 1, "shots_stationary": 1}
