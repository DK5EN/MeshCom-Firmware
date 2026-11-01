#!/usr/bin/env python3
"""Claim 4: CRC error share, CRC RSSI/SNR, freq error per last hop (decoded frames), HB9LEK-12 offset,
CRC errors in the 4.5 kHz bin.

Usage: python3 -I crc_ferr.py <norm-dir>
Good receptions = ONRXDONE_TIME lines (includes 12-byte ACKs); decoded frames = MH-LoRa lines, each paired
with the closest preceding '[LoRa]...Received packet' line (freq error, RSSI).
"""
import re
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

CRC = re.compile(r"CRC_ERROR rssi=(-?\d+) snr=(-?\d+) freq_err=(-?[\d.]+)")
PKT = re.compile(r"Received packet: RSSI:\s+(-?\d+) dBm / SNR:\s+(-?\d+) dB / Frequency error:\s+(-?[\d.]+)")
MH = re.compile(r"MH-LoRa\s+\d+ . x[0-9A-F]{8} H\d\d \S+ \S+ \S+ ([^>]+)>")


def q(v: list[float], p: float) -> float:
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))]


def run(p: Path) -> None:
    crc: list[tuple[int, int, float]] = []
    good = 0
    last_pkt: tuple[int, int, float] | None = None
    per: dict[str, list[float]] = defaultdict(list)
    dec_rssi: list[int] = []
    with p.open(errors="replace") as fh:
        for line in fh:
            if "CRC_ERROR" in line:
                m = CRC.search(line)
                if m:
                    crc.append((int(m.group(1)), int(m.group(2)), float(m.group(3))))
            elif "ONRXDONE_TIME" in line:
                good += 1
            elif "Received packet" in line:
                m = PKT.search(line)
                if m:
                    last_pkt = (int(m.group(1)), int(m.group(2)), float(m.group(3)))
            elif "MH-LoRa" in line and last_pkt is not None:
                m = MH.search(line)
                if m:
                    per[m.group(1).split(",")[-1]].append(abs(last_pkt[2]))
                    dec_rssi.append(last_pkt[0])
                    last_pkt = None
    n = len(crc)
    print(f"== {p.name}: CRC_ERROR {n}, good (ONRXDONE_TIME) {good}, share {100*n/(n+good):.1f} %")
    r = [c[0] for c in crc]
    s = [c[1] for c in crc]
    f = [abs(c[2]) for c in crc]
    print(f"   CRC rssi p50 {st.median(r)} max {max(r)}; snr p50 {st.median(s)}; |ferr|<1000 {100*sum(x<1000 for x in f)/n:.1f} % >3000 {100*sum(x>3000 for x in f)/n:.1f} %")
    print(f"   CRC |ferr| in 4000-5000 Hz: {sum(4000<=x<5000 for x in f)}; >3000: {sum(x>3000 for x in f)}")
    print(f"   decoded rssi p50 {st.median(dec_rssi)} max {max(dec_rssi)}")
    top = sorted(((k, v) for k, v in per.items() if len(v) >= 40), key=lambda kv: -st.median(kv[1]))[:5]
    for k, v in top:
        print(f"   last hop {k}: n={len(v)} median |ferr| {st.median(v):.0f} Hz, >3000 Hz {100*sum(x>3000 for x in v)/len(v):.1f} %")


def main() -> None:
    d = Path(sys.argv[1])
    run(d / "hb9vqq4.log")
    run(d / "hb9vqq5.log")


if __name__ == "__main__":
    main()
