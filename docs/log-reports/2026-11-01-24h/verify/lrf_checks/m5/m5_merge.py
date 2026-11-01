#!/usr/bin/env python3
"""M5: merge the outputs of m5_rf.py and m5_earlier.py with the recorded wind-data finding (d) into
data/extra/lrf_m5.json, and add the distribution of silence starts over the logger-local hour of day.
Usage: python3 -I m5_merge.py <rf.json> <earlier.json> <out.json>
"""
from __future__ import annotations
import collections, datetime as dt, json, sys
from pathlib import Path

WIND = {
    "result": "NO WIND DATA in mcmap-prod; no gust test possible",
    "queries_read_only": [
        "wx_telemetry_series call=OE3XIA-12 hours=2 -> fields returned: timestamp,temp,temp2,hum,press,qnh,alt,batt,gas_res,eco2,wlevel,wtemp (OE3XIA-12 itself: only alt=613, batt=100)",
        "wx_stations (no limit) -> total 507 (kind wx 504, channel 3); per-station keys: call,channelCount,datapoints,kind,lastHum,lastTemp,lastTemp2,lastTimestamp,temp2NonzeroCount; no coordinates, no wind",
        "wx_telemetry_series schema field list: temp,temp2,hum,press,qnh,qnhReduced,alt,altSmoothed,batt,gas_res,eco2,wlevel,wtemp (no wind speed, no gust)",
        "telemetry_channels for the only 4 stations with channelCount>0 (OE1KBC-99, OE3LCR-55, OE3XWJ-93, SQ9APP-1): each ch50 Versorgungsspannung V, ch51 GSM Signalstaerke dB, ch60 Pegel cm, ch65 Wassertemperatur C (a river gauge, Stiefern); no wind",
    ],
    "not_called": "wx_field (no wind field in its documented schema; returns ~300 stations, no filter), all write tools (maintenance_*, box_disable, box_reset)",
    "proxy_available": "none for gusts; pressure (press/qnh) only shows fronts, not gusts, and was not used",
}


def main() -> None:
    rf = json.loads(Path(sys.argv[1]).read_text()); early = json.loads(Path(sys.argv[2]).read_text())
    hours = collections.Counter(dt.datetime.fromtimestamp(s["start_epoch_local"], dt.timezone.utc).hour for s in rf["silences"]["list"])
    rf["silence_starts_by_local_hour"] = {f"{h:02d}": hours.get(h, 0) for h in range(24)}
    rf["silence_starts_night_22_to_06"] = sum(v for h, v in hours.items() if h >= 22 or h < 6)
    rf["d_wind"] = WIND
    rf["e_earlier_captures"] = {"searched": ["logs/ (recursive)", "~/meshlog/ (recursive)"],
        "oe3xia_logger_files": ["logs/berg-node/oe3xia_meshcom_2026-09-01_220932.log", "logs/20261101-24h-log/meshcom_oe3xia_2026-10-31_064003.log"],
        "other_files_mentioning_OE3XIA-12_as_hop_only": ["logs/berg-node/oe3xoc_*.log", "logs/berg-node/oe3xwj_*.log", "~/meshlog/dk5en-98-local/window-0927neo.log", "~/meshlog/dk5en-98-local/2026-09-27.log", "~/meshlog/rpizero/dk5en-98/2026-09-27.log"],
        "proxy_results": early}
    Path(sys.argv[3]).write_text(json.dumps(rf, indent=1))
    print("starts by local hour:", rf["silence_starts_by_local_hour"], "night(22-06):", rf["silence_starts_night_22_to_06"])


if __name__ == "__main__":
    main()
