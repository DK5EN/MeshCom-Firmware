# M5: OE3XIA-12 RF path vs stuck receiver (campaign docs/campaign-logreport-fixes-20261101.md)

Verdict: the RF path (antenna, coax, TX power) shows no degradation inside the 35 silences. Stuck receiver
state stays the better explanation. Wind could not be tested (mcmap holds no wind data). Confidence: high
that TX and the shared antenna path are unchanged, medium that nothing RX-only (LNA, RF switch) differs.

Run (about 50 s per script, `$O` = any scratch dir, `N` = norm dir). All times are logger-local (CET).

    N=logs/20261101-24h-log/.logreport-work/norm; C=docs/log-reports/2026-11-01-24h/verify/lrf_checks/m5
    python3 -I $C/m5_rf.py $N $O/rf.json
    python3 -I $C/m5_earlier.py $O/earlier.json sep01 logs/berg-node/oe3xia_meshcom_2026-09-01_220932.log \
        logs/berg-node/oe3xoc_meshcom_2026-09-01_221146.log logs/berg-node/oe3xwj_meshcom_2026-09-01_220645.log \
        -- oct31 $N/oe3xia.log $N/oe3xoc.log $N/oe3xwj.log $N/oe3xhl.log
    python3 -I $C/m5_merge.py $O/rf.json $O/earlier.json docs/log-reports/2026-11-01-24h/data/extra/lrf_m5.json

Output line of `m5_rf.py`: `silences: 35 total 15620 s longest 1024 s first 10-31 08:26:28` (reproduces
health.md exactly). Definition: gap > 300 s between consecutive ONRXDONE_TIME / RX_ERROR lines of oe3xia.log
(ACKs included); the silence is the open interval between the two receptions.

## (a) RSSI/SNR at the other loggers, last hop = OE3XIA-12

Frame = `[LOG]` reception line with RSSI/SNR whose path's last callsign is OE3XIA-12 (all receptions, DUP:d
too). Outside = same time span (OE3XIA log and receiving log both exist), not inside a silence. Quartiles are
inclusive linear interpolation; p = two-sided Mann-Whitney, normal approximation with tie correction.

- OE3XHL: inside n=34, SNR median -19 (IQR -19..-18), RSSI -113 (-113..-113); outside n=1369, SNR -18
  (-19..-18), RSSI -113. p(SNR)=0.42, p(RSSI)=0.83. Mean SNR -18.53 vs -18.44.
- OE3XIR: inside n=10, SNR 3 (3..3), RSSI -103; outside n=319, SNR 3 (3..4), RSSI -103 (-103..-102).
  p(SNR)=0.056, p(RSSI)=0.42. Mean SNR 2.2 vs 3.1 (8 of 10 inside frames at 3, the outside mode).
- OE3XOC: inside n=27, SNR -2 (-2..-2), RSSI -109; outside n=987, SNR -2 (-3..-2), RSSI -109. p(SNR)=0.055,
  p(RSSI)=0.47. Mean SNR -2.15 vs -2.52 (inside 0.4 dB better; p=0.027 against the daytime-only baseline).
- OE3XWJ, hb9vqq4, hb9vqq5: 0 frames with last hop OE3XIA-12 (not in range), no statement possible.
- Largest inside/outside shift: 1 dB in the median at one logger (XHL), 0 dB at XOC and XIR; no sign of a
  3 dB or larger loss. Control (`lrf_m5.json` `control_other_last_hops`): other last hops at the same loggers show p < 0.01
  with equal or 1-dB-shifted medians (OE3GJC-12 at XHL: -111 vs -112 dBm), so such small shifts are quantisation
  and receiver-side jitter, not an OE3XIA effect.
- Caveat: RSSI is quantised and near-constant per link (XHL: 1367 of 1369 outside frames at -113), so SNR is
  the usable metric. Frame rate heard at the neighbours drops with OE3XIA's TX rate (XHL 7.8 vs 66.7 per h).

## (b) Heard share of OE3XIA TX

Heard = same msg_id at one of oe3xhl/xir/xoc/xwj within 30 s, last hop OE3XIA-12 (same method inside and out).
ACK TX (type A, 13 of the 57) have no path at the receiver and cannot be matched: the "40/57" mixes in 13
unmatchable ACKs. Comparable figure is non-ACK TX.

- Inside: 40 of 57 TX (70.2 %); non-ACK 40 of 44 = 90.9 % (Wilson 95 %: 78.8..96.4).
- Outside (full capture): 1710 of 3942 (43.4 %, 1911 ACK TX unmatchable); non-ACK 1710 of 2031 = 84.2 %
  (82.5..85.7). Fisher p (non-ACK) = 0.30. All four loggers present (06:47-18:16): 22/24 = 91.7 % inside vs
  772/927 = 83.3 % outside, p = 0.41.
- By type inside: 21/23 (@), 14/16 (:), 5/5 (!). TX rate: 13.1 per h inside vs 191.3 per h outside.

## (c) CAD busy (CAD_SCAN result=-702)

- OE3XIA inside 161 of 218 = 73.9 % (Wilson 67.6..79.2); outside 18889 of 22831 = 82.7 %; Fisher p = 0.001.
- Control, same wall-clock windows at the other loggers (inside vs outside): XHL 36.3 vs 45.9 %, XIR 71.3 vs
  76.1 %, XOC 71.6 vs 75.0 %, XWJ 53.9 vs 54.4 %. OE3XIA's relative drop (0.89) lies inside the range 0.79..0.99
  of the other nodes: the channel was a bit less busy in those hours everywhere, and OE3XIA's CAD saw it.

## (d) Wind

No wind or gust data exists in mcmap-prod, so no gust-at-silence-start test was run. Read-only queries
(listed in `lrf_m5.json` `d_wind`): `wx_telemetry_series` fields are temp, temp2, hum, press, qnh, alt, batt,
gas_res, eco2, wlevel, wtemp (OE3XIA-12 itself: alt and batt only); `wx_stations` (507 stations) has no
coordinates; the four stations with generic channels (OE1KBC-99, OE3LCR-55, OE3XWJ-93, SQ9APP-1) carry a
river gauge (supply V, GSM, level, water temperature). Wind would need an external source (for example
GeoSphere Austria station data for Exelberg/Wien) and the 35 start times from `lrf_m5.json`.
Weak hint against a weather-only cause: starts are spread over all hours (10 of 35 between 22:00 and 06:00).

## (e) Earlier captures

- Only one earlier OE3XIA logger file exists: `logs/berg-node/oe3xia_meshcom_2026-09-01_220932.log`
  (09-01 22:09 to 09-02 06:17, 8.1 h, night only). `~/meshlog/` has none (OE3XIA-12 appears there only as a hop).
- That build has no ONRXDONE_TIME / RX_ERROR / CAD lines. Proxy: gap > 300 s between `[LOG] ddd` reception
  lines. Result: 1 gap, 300.5 s (09-01 22:36:15..22:41:16; XOC logged 15, XWJ 46 receptions meanwhile);
  next longest 174.5 s. Same proxy on 31.10.: 37 gaps, 17135 s, longest 1455 s. So no comparable silence
  before 31.10., but the evidence is thin: 8 h at night, older firmware, proxy without RX_ERROR.

## Reading

- RF path: TX heard as well inside as outside (90.9 vs 84.2 % non-ACK), SNR at the neighbours unchanged
  within 1 dB, CAD works. A loose coax or wind-induced antenna loss would hit TX and CAD too.
- Not excluded: an RX-only fault (LNA or RF switch state) that leaves TX and CAD intact. Only the planned
  `RX_SILENCE` snapshot (`SX126xGetRssiInst()`, op mode, IRQ, device errors) separates this from a stuck chip.
