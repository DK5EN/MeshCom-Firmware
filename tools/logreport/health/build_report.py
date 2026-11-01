#!/usr/bin/env python3
"""Merge the helper JSONs into results.json and render findings.md (English, verdict table per node).

Usage (via run.py): build_report.py
Inputs  (all in <out>): results.json (node_baseline.py), cet_beacons.json, strong_signals.json,
        ack_attribution.json, rx_gaps.json, <standalone node>_eth.json
Outputs: results.json (augmented, adds per-node time_signal/strong_signals/ack_attribution/eth04 and network{}),
         findings.md
Verdict rules are explicit in `verdicts()`; status words: OK / Beobachten / Handlungsbedarf.
"""
from __future__ import annotations

import datetime as dt
import json
from typing import Any

from nodeset import ALL as NODES, CLUSTER as OE3, OUT, STANDALONE

# the narrative findings.md is written for exactly this capture set (2026-10-30/31 -> 11-01)
NARRATIVE_NODES = {"oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj", "db0ais"}
THEORY_FLOOR = -114


def f1(x: Any, nd: int = 1) -> str:
    return "n/a" if x is None else (f"{x:.{nd}f}" if isinstance(x, float) else str(x))


def pct(a: float, b: float, nd: int = 1) -> str:
    return f"{100.0 * a / b:.{nd}f}%" if b else "n/a"


def worst(*s: str) -> str:
    order = {"OK": 0, "Beobachten": 1, "Handlungsbedarf": 2}
    return max(s, key=lambda x: order[x])


def verdicts(n: str, r: dict[str, Any], extra: dict[str, Any]) -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    sm, tr, tx, ch, cad, rf, ring = r["state_machine"], r["traffic"], r["own_tx"], r["channel"], r["cad"], r["rf"], r["ring"]
    st, gw, eth, fc, hl = r["stat"], r["gateway"], r["eth"], r["firmware_census"], r["health"]
    cu = ch["CHANNEL_UTIL"]
    rows.append(("State machine", "OK" if sm["errors_other_n"] == 0 else "Handlungsbedarf",
                 f"{sm['csma_backoff_rc_minus1']} x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); "
                 f"{sm['errors_other_n']} other rc!=0 / ERROR; {sm['interleaved_garbage_lines']} console-interleaved lines ignored"))
    stale = sum(ring["drop_stale_by_prio_reason"].values())
    nonstale = sum(ring["drops_non_stale"].values())
    rows.append(("Relay", "OK" if nonstale == 0 and ring["ring_overflow"] == 0 else "Handlungsbedarf",
                 f"{tx['src_split'].get('r', 0)} of {tx['tx_lines']} TX are relays ({tx['src_pct'].get('r', 0)}%); RING_OVERFLOW {ring['ring_overflow']}, "
                 f"RING_DROP_PRIO/NEW {nonstale}; TTL drops {stale} (prio5 HEY/prio4 position, by design); ring peak {ring['ring_write_queued_max']}/{ring['MAX_RING']}; "
                 f"RLY refusals {r['own_tx']['relay_decisions_RLY']}"))
    wrap = sum(v for k, v in tr["hop_H_field"].items() if int(k[1:]) >= 13)
    rows.append(("Hop counter", "Beobachten" if wrap else "OK",
                 f"H field {tr['hop_H_pct']}; {wrap} frames with H>=13 (wrap from foreign builds)" if wrap else f"H field {tr['hop_H_pct']}; no H>=13 wrap"))
    own_e = tr["own_in_path"].get("e", 0)
    rows.append(("Loops", "OK", f"RELAY_LOOP_BLOCKED {ring['relay_loop_blocked']} (gate working, not a fault); {own_e} receptions ({pct(own_e, tr['heard_events'])}) carry own call in path (echo of own/relayed frames)"))
    cu_status = "OK" if cu["mean"] < 40 else ("Beobachten" if cu["mean"] < 60 else "Handlungsbedarf")
    rows.append(("Channel util", cu_status,
                 f"CHANNEL_UTIL mean {cu['mean']}%, median {cu['median']}%, p95 {cu['p95']}%, peak {cu['max']}% ({cu['ge70']} samples >=70%, {cu['ge90']} >=90%); "
                 f"1st/2nd half {cu['first_half_mean']}/{cu['second_half_mean']}%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative "
                 f"({ch['inflation_check']['rx_values_multiple_of_2476ms']} of {ch['inflation_check']['rx_values_nonzero']} rx values are multiples of 2476 ms). "
                 f"Cross-check: own TX airtime {ch['own_tx_airtime_s_from_TX_lines']:.0f} s (ToA of TX lines) = {ch['own_tx_airtime_s_from_CHANNEL_UTIL']:.0f} s (CHANNEL_UTIL) = {ch['own_tx_airtime_s_from_STAT']:.0f} s (STAT); "
                 f"STAT rx {ch['rx_airtime_s_from_STAT']:.0f} s vs ToA of decoded frames {ch['rx_airtime_s_decoded_frames']:.0f} s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it"))
    rt = ring["max"]["retrying"]
    rows.append(("Retries", "OK" if rt == 0 else "Beobachten",
                 f"RING_STATUS retrying max {rt} over {ring['ring_status_samples']} samples; pending max {ring['max']['pending']}; ring high-water {st['ringmax_max']}/{st['ringmax_of']}; RETRANSMIT_GIVEUP {ring['retransmit_giveup']}"))
    ershare = rf["rx_error_share_of_rx_pct"]
    crc_status = "OK" if ershare < 40 else "Beobachten"
    rows.append(("CRC / RX errors", crc_status,
                 f"{rf['rx_error_n']} RX_ERROR (preamble/header seen, no valid frame) = {rf['rx_error_per_hour_mean']}/h = {ershare}% of (errors + OnRxDone); median RSSI {rf['rx_error_rssi']['median']} dBm, "
                 f"median SNR {rf['rx_error_snr']['median']} dB -> noise-limited. {rf['rx_error_gt_minus85']} errors above -85 dBm, {rf['rx_error_gt_minus60']} above -60 dBm. "
                 f"nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers ({rf['rx_irq_stale']['rssi_lines']})"))
    miss = gw["beat_miss_pct_of_keepalives"]
    net_status = "OK" if miss < 1.0 else "Beobachten"
    extra_net = ""
    if eth["events_count"].get("link"):
        extra_net = f"; {eth['events_count']['link'] // 2} ETH link flaps (1-4 s)"
    dhcp = eth["counters_final"]["renews"] if eth["counters_final"] else 0
    rows.append(("Network (ETH/GW)", net_status,
                 f"keepalive [GW];keep ok {gw['keepalive_ok']} (expected ~{gw['keepalive_expected_30s']:.0f}, max gap {gw['keepalive_gaps']['max_all_s']} s); "
                 f"{gw['beat_after_keep_missing']} keepalives ({miss}%) without server BEAT reply, '[UDP] Server not responding for 35s' {gw['udp_server_not_responding_n']}x; "
                 f"ETH tx_fail {eth['counters_final']['tx_fail']}" + (" (already in the first 60-s summary = boot-time first send, max 1.6 s)" if eth['first'] and eth['first']['tx_fail'] == eth['counters_final']['tx_fail'] and eth['counters_final']['tx_fail'] else "") + f", DHCP renews {dhcp}, renew_fail {eth['counters_final']['renew_fail']}, hw resets {eth['counters_final']['resets']}{extra_net}; "
                 f"NTP ok {gw['ntp']['ok']}/timeouts {gw['ntp']['timeout']}"))
    ups = st["uptime_resets"]
    rows.append(("Crashes / reboots", "OK" if not ups and not hl["crash_hits"] else "Handlungsbedarf",
                 f"uptime monotonic ({st['up_first']} s at first STAT .. {st['up_last']} s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: {len(hl['boot_banner_hits'])}"
                 + ("; single boot at capture start (RESETREAS=0x4)" if n == "db0ais" else f"; node had booted {st['boot_before_capture_start_s']} s before the first logged line (first STAT up=300 s at {st['boot_wall_est']} est.)")))
    hp = st["heap"]
    rows.append(("Heap", "OK" if hp and abs(hp["slope_per_hour"]) < 50 else "Beobachten",
                 f"STAT heap {hp['first']} -> {hp['last']} B (min {hp['min']}, max {hp['max']}), slope {hp['slope_per_hour']} B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse)"
                 + ("; one step -1068 B at 10-31 07:59:45, then flat for 287 samples (not a trend)" if n == "oe3xoc" else "")))
    g3 = cad["attempt_ge3_pct"]
    cad_status = "OK" if g3 < 10 else ("Beobachten" if g3 < 30 else "Handlungsbedarf")
    if cad["cad_safety_timeout"] >= 3 and cad_status == "OK":
        cad_status = "Beobachten"
    rows.append(("CSMA / CAD", cad_status,
                 f"TX_GATE_ENTER {cad['tx_gate_enter']}: attempt0 {cad['attempt0_pct']}%, >=3: {g3}%, max {cad['attempt_max']}; CAD initial busy {cad['initial_busy_rate_pct']}% of {cad['cad_scans']} scans, "
                 f"false positive {cad['false_positive_pct_of_initial_busy']}% of initial busy; {cad['storm_episodes_attempt_ge3']} episodes with attempt>=3 (longest {cad['storm_dur_s'].get('max')} s); "
                 f"CAD_SAFETY_TIMEOUT {cad['cad_safety_timeout']} (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED {cad['csma_aged']}"))
    rows.append(("Double send", "OK" if ring["double_send_bug_type_00_then_FF_same_slot"] == 0 else "Handlungsbedarf",
                 f"bug-type (same slot, READY then DONE) {ring['double_send_bug_type_00_then_FF_same_slot']}; {ring['ring_tx_read_ids_gt1']} msg_ids read more than once"
                 + ("; own ping ids recur ~16 h apart (e.g. F5CE712E at 06:27:25 and 22:37:25): ID reuse >4 h, not a defect" if n == "oe3xwj" else "; recurrences are >4 h apart or legit retransmit copies")))
    pre_ev = fc["events_pre_cad_pct"]
    fw_status = "OK" if pre_ev < 10 else ("Beobachten" if pre_ev < 25 else "Handlungsbedarf")
    rows.append(("Firmware without CAD", fw_status,
                 f"{fc['nodes_pre_cad']} of {fc['nodes_total']} originators ({fc['nodes_pre_cad_pct']}%) on pre-CAD (4.35a-o, 4.34 and older); they carry {pre_ev}% of heard frame events, "
                 f"{fc['unique_msgs_pre_cad_pct']}% of unique msgs, {fc['airtime_pre_cad_pct']}% of decoded airtime (own-call echoes and time servers excluded)"))
    p5 = rf["rx_error_rssi"]["p5"]
    gap = p5 - THEORY_FLOOR
    nf_status = "OK" if gap <= 6 else ("Beobachten" if gap <= 12 else "Handlungsbedarf")
    rows.append(("Noise floor", nf_status,
                 f"carpet = RX_ERROR RSSI p5 {p5:.0f} dBm (median {rf['rx_error_rssi']['median']:.0f}) -> {gap:.0f} dB above the -114 dBm BW250 theory; "
                 f"RSSI-SNR estimator (SNR>=0 frames, n={rf['noise_rssi_minus_snr_snr_ge0']['n']}) {rf['noise_rssi_minus_snr_snr_ge0']['median']:.0f} dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note"))
    ss = extra["strong_signals"]
    mx = rf["rssi_all"]["max"]
    in_status = "OK"
    note = ""
    if ss["err_strong"] > 50:
        in_status = "Beobachten"
        note = (f"; {ss['err_strong']} RX_ERROR + {ss['decoded_strong']} decoded receptions above -60 dBm, spread over every hour of the capture (SNR median {ss['snr_err_strong_median']} dB) "
                f"-> persistent in-band interferer or strong non-LoRa carrier, not a neighbour")
    rows.append(("Input level neighbours", in_status,
                 f"strongest decoded RSSI {mx:.0f} dBm; {rf['rx_rssi_gt_minus20']} receptions above -20 dBm (overload limit), {rf['rx_rssi_gt_minus40']} above -40 dBm; 10-dB histogram {rf['rssi_hist_10dB']}{note}"))
    rg = r["rx_silence"]
    lg0 = rg["longest_gaps"][0]
    sil_status = "Handlungsbedarf" if rg["gaps_gt_300s"] >= 5 else ("Beobachten" if lg0["gap_s"] > 150 else "OK")
    sil_txt = (f"median gap between receptions {rg['median_gap_s']} s, longest {lg0['gap_s']:.0f} s (at {lg0['start']}); {rg['gaps_gt_300s']} silences >300 s"
               + (f" totalling {rg['silent_seconds_in_gaps_gt_300s']} s ({rg['silent_pct_of_span']}% of the capture)" if rg['gaps_gt_300s'] else ""))
    if n == "oe3xia":
        sil_txt += ("; all 35 began 10-31 08:26 or later (none in the first 1 h 46 min), in every one each other OE3 node still logging kept receiving (16-309 events per window), "
                    "no OnRxDone and no OnRxError at all, while CAD kept sensing the channel busy (77% of 265 scans vs 83% outside) and own TX continued -> receiver path stuck/desensitised, not a quiet band (inference)")
    if n == "oe3xwj":
        sil_txt += ("; the 153 s silence at 10-31 17:03:38 has 22 CAD_SAFETY_TIMEOUT (CadDone never fired, src/nrf52/nrf52_main.cpp:1790), 0 own TX, and two 498 ms "
                    "`[ETH];stall;udp_rx` (17:04:54, 17:05:56); the radio recovered by itself at 17:06:06 (fact); shared SPI/IRQ starvation is a hypothesis")
    rows.append(("Receive silences", sil_status, sil_txt))
    ts = extra["time_signal"]
    key = "OE1XAR-62" if n == "db0ais" else "OE1XAR-33"
    gwg = ts["gw_rx_gaps"].get(key, {})
    lg = ts["lora_gaps_by_src"].get(key, {})
    miss_b = gwg.get("missing_beacons", 0)
    ts_status = "OK" if miss_b <= 2 else "Beobachten"
    rows.append(("Time signal delivery", ts_status,
                 f"Internet side ([CET];gw_rx from {key}): {gwg.get('n_distinct')} of ~{gwg.get('expected_in_span')} (median gap {gwg.get('median_gap_s'):.0f} s), {miss_b} missing; "
                 f"LoRa side: {lg.get('n_distinct')} distinct stamps heard, {lg.get('missing_beacons')} gaps"
                 + (f"; coverage of OE3 union {ts['lora_oe1xar33_heard_of_union']['pct']}%" if 'lora_oe1xar33_heard_of_union' in ts else "")))
    ah = tr["ack_h_outside_80_84"]
    na = tr["ack_events"]
    rows.append(("ACK plausibility", "OK" if na == 0 or ah / na < 0.02 else "Beobachten",
                 f"{ah} of {na} ACK receptions ({pct(ah, na, 2)}) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers {ring['ack_reject']}"))
    on = r["onrxdone"]["ms"]
    rows.append(("OnRxDone timing", "OK" if r["onrxdone"]["gt50ms"] <= 0.001 * on["n"] else "Beobachten",
                 f"ONRXDONE_TIME mean {on['mean']} ms, median {on['median']}, p95 {on['p95']}, max {on['max']} ms; {r['onrxdone']['gt50ms']} of {on['n']} above 50 ms (MC-WARN ONRXDONE_SLOW)"))
    return rows


def node_section(n: str, r: dict[str, Any], extra: dict[str, Any]) -> list[str]:
    i, sp, cl, tr, tx, nb = r["identity"], r["span"], r["clock"], r["traffic"], r["own_tx"], r["neighbours"]
    gw, fc, ch, cad, rf = r["gateway"], r["firmware_census"], r["channel"], r["cad"], r["rf"]
    out: list[str] = []
    out.append(f"## {i['callsign']}  (log: {n}.log)\n")
    clk_off = cl["offset_node_minus_logger_s_mode"]
    clock_txt = f"node clock minus logger clock: {clk_off // 3600:+d} h (mode)"
    if n == "db0ais":
        clock_txt = ("node clock = UTC+2 from boot until ~18:20:3x logger time, then UTC+1 (step of exactly -1 h, no log line names a cause); "
                     "logger = PuTTY PC clock UTC+1")
    elif n == "oe3xir":
        clock_txt += " (node on UTC+2 for the whole capture; logger = UTC+1 per NTP)"
    else:
        clock_txt += " (node on UTC+1 = logger)"
    out.append("| Item | Value |\n|---|---|")
    out.append(f"| Callsign / GW-ID | {i['callsign']} / {i['gw_id']} (from `[KEEP]` line; `[GW];keep` itself carries no call) |")
    out.append(f"| Hardware | {i['hw_name']} (HW id {i['hw_id']}, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |")
    out.append(f"| Role | Internet gateway + relay: keepalives {gw['keepalive_ok']}, uploads GWU {i['own_gateway_role']['gwu_upload']}, server->mesh injections GWI {i['own_gateway_role']['gwi_inject']}, BEAT replies {gw['beat_rx']} |")
    out.append(f"| Firmware | {i['version_keep']} (STAT `fw=` {', '.join(i['stat_fw'].keys())} = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |")
    if n == "db0ais":
        lora = "433.175 MHz, BW 250 kHz, SF11, CR 4/6, preamble 8, TX 22 dBm (boot block `[LoRa]...RF_*`)"
    else:
        lora = ("not logged (no boot block). SF11/BW250/CR4:6 inferred: ToA of the TX lines matches CHANNEL_UTIL tx= "
                f"({ch['own_tx_airtime_s_from_TX_lines']:.0f} s vs {ch['own_tx_airtime_s_from_CHANNEL_UTIL']:.0f} s); frequency/power not in log (fleet standard 433.175 MHz)")
    out.append(f"| LoRa | {lora} |")
    out.append(f"| Capture span | {sp['first']} -> {sp['last']} ({sp['hours']} h, {sp['lines']} lines; {sp['clock_basis']}) |")
    out.append(f"| Clock | {clock_txt} |")
    out.append("")
    out.append("### Traffic (receptions, not accepted packets; FCS:0000 excluded)\n")
    out.append(f"- Heard frame events {tr['heard_events']} (+ {tr['ack_events']} ACK frames = {tr['ack_share_of_all_frames_pct']}% of all receptions; {tr['fcs0000_excluded']} FCS:0000 decoder bailouts excluded). "
               f"New {tr['dup_flag'].get('n', 0)} / duplicate copies {tr['dup_flag'].get('d', 0)}.")
    out.append(f"- Unique originators {tr['unique_originators_excl_time_server']} (excl. time servers; includes own-call echoes), unique msg_ids {tr['unique_msgids']}. Types: "
               + ", ".join(f"{v['label']} {v['n']} ({v['pct']}%)" for v in tr["types"].values()) + ".")
    out.append(f"- Decoded airtime by type: {tr['airtime_share_by_type_pct']} (ACK frames are 012-byte, ~313 ms each).")
    out.append(f"- Hop: H field (remaining max_hop) {tr['hop_H_pct']}; path length (callsigns) {tr['path_len_pct']}; zero-relay (path length 1) {tr['direct_zero_relay_pct']}%.")
    out.append(f"- Own TX {tx['tx_lines']} ({tx['tx_per_hour']}/h, {tx['airtime_s']} s airtime): own {tx['src_split'].get('o', 0)} / relay {tx['src_split'].get('r', 0)} / gateway(server->mesh) {tx['src_split'].get('g', 0)}; "
               f"by type {tx['type_split']}, by prio {tx['prio_split']}; queue wait median {tx['wait_ms']['median']:.0f} ms, p95 {tx['wait_ms']['p95']:.0f} ms; CAD attempts at TX start {tx['cad_at_tx']}.")
    out.append("")
    out.append(f"Direct neighbours = last path element ({nb['n_direct']} distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):\n")
    out.append("| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |\n|---|---|---|---|---|---|---|")
    for x in nb["top"][:10]:
        out.append(f"| {x['call']} | {x['n']} | {x['rssi_median']:.0f} ({x['rssi_p10']:.0f}..{x['rssi_p90']:.0f}, {x['rssi_max']}) | {x['snr_median']:.0f} | {x['hw'] or 'n/a'} | {f1(x['dist_km'])} | {(x['desc'] or '')} |")
    out.append("")
    out.append("### Channel / RF\n")
    out.append(f"- Noise: RX_ERROR RSSI p5/median {rf['rx_error_rssi']['p5']:.0f}/{rf['rx_error_rssi']['median']:.0f} dBm; RSSI-SNR (SNR>=0) median {rf['noise_rssi_minus_snr_snr_ge0']['median']:.0f} dBm; "
               f"RX_ERROR histogram {rf['rx_error_hist_10dB']}.")
    out.append(f"- CAD: {cad['cad_scans']} scans, initial busy {cad['initial_busy_rate_pct']}%, confirmed busy {cad['confirmed_busy_pct_of_initial_busy']}% of initial busy, false positive {cad['false_positive_pct_of_initial_busy']}%. "
               f"cad_attempt distribution (TX_GATE_ENTER): 0:{cad['cad_attempt_pct'].get('0')}% 1:{cad['cad_attempt_pct'].get('1')}% 2:{cad['cad_attempt_pct'].get('2')}% >=3:{cad['attempt_ge3_pct']}%.")
    out.append(f"- util by hour (CHANNEL_UTIL mean %): {ch['util_by_hour']}")
    out.append("")
    out.append("### Firmware census of heard originators\n")
    out.append(f"- Pre-CAD {fc['nodes_pre_cad']}/{fc['nodes_total']} nodes ({fc['nodes_pre_cad_pct']}%), {fc['events_pre_cad_pct']}% of events, {fc['airtime_pre_cad_pct']}% of airtime. "
               f"Versions: {fc['versions_nodes']}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)")
    out.append("")
    out.append("| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |\n|---|---|---|---|---|---|---|---|")
    for t in fc["top_pre_cad"][:8]:
        out.append(f"| {t['call']} | {t['fw']} | {t['events']} | {t['msgs']} | {t['events_pct_of_nodes']}% | {t['hw'] or 'n/a'} | {t['desc'] or ('lat/lon ' + str(t['lat']) + ',' + str(t['lon']) if t['lat'] else 'no master data')} | {f1(t['dist_km'])} |")
    out.append("")
    out.append("### Gateway / server path\n")
    out.append(f"- BEAT miss episodes {gw['beat_miss_episodes_n']} ({gw['beat_after_keep_missing']} keepalives = {gw['beat_miss_pct_of_keepalives']}%); BEAT gaps >65 s: {gw['beat_gaps_gt65s']['n_gt_thr']} (max {gw['beat_gaps_gt65s']['max_s']} s); "
               f"clusters of 'Server not responding': {gw['udp_clusters_n']}.")
    if gw["beat_miss_per_hour"]:
        top_h = sorted(gw["beat_miss_per_hour"].items(), key=lambda kv: -kv[1])[:5]
        out.append(f"- Busiest hours for missing BEAT (logger clock): {top_h}")
    out.append(f"- ETH: IP {', '.join(r['eth']['ips_seen'])} -> server {', '.join(r['eth']['dest_seen'])}; hb_age_s median {r['eth']['hb_age_s']['median']:.0f} / max {r['eth']['hb_age_s']['max']:.0f}; "
               f"stalls {json.dumps({k: (v['n'], v['ms'].get('max')) for k, v in r['eth']['stalls'].items()})}; events {r['eth']['events_count']}.")
    out.append("")
    out.append("### Verdict table\n")
    out.append("| Aspect | Status | Comment |\n|---|---|---|")
    for a, s, c in verdicts(n, r, extra):
        out.append(f"| {a} | {s} | {c} |")
    out.append("")
    return out


def main() -> None:
    hd = OUT
    res = json.loads((hd / "results.json").read_text())
    cet = json.loads((hd / "cet_beacons.json").read_text())
    strong = json.loads((hd / "strong_signals.json").read_text())
    ack = json.loads((hd / "ack_attribution.json").read_text())
    eth04 = {n: json.loads((hd / f"{n}_eth.json").read_text()) for n in STANDALONE}
    nodes = res["nodes"]
    gaps = json.loads((hd / "rx_gaps.json").read_text())
    for n in NODES:
        nodes[n]["rx_silence"] = gaps[n]
    for n in NODES:
        nodes[n]["time_signal"] = cet[n]
        nodes[n]["strong_signals"] = strong[n]
        nodes[n]["ack_attribution"] = ack[n]
    for n in STANDALONE:
        nodes[n]["eth04"] = eth04[n]

    # network-level aggregates
    ping_share = {n: (ack[n]["top_acked_prefix"][0]["share_pct"] if ack[n]["top_acked_prefix"] else None) for n in OE3}
    ping_air = {n: nodes[n]["traffic"]["airtime_share_by_type_pct"].get("ACK") for n in NODES}
    # common BEAT-miss event across OE3 gateways (episodes starting within 75 s of each other, >=3 nodes)
    evs = []
    for n in OE3:
        for e in nodes[n]["gateway"]["beat_miss_episodes"]:
            evs.append((dt.datetime.strptime("2024-" + e["start"], "%Y-%m-%d %H:%M:%S"), n))
    evs.sort()
    common: list[dict[str, Any]] = []
    cur: list[tuple[dt.datetime, str]] = []
    for t, n in evs:
        if cur and (t - cur[-1][0]).total_seconds() > 75:
            if len({x[1] for x in cur}) >= 3:
                common.append({"start": cur[0][0].strftime("%m-%d %H:%M:%S"), "nodes": sorted({x[1] for x in cur})})
            cur = []
        cur.append((t, n))
    if len({x[1] for x in cur}) >= 3:
        common.append({"start": cur[0][0].strftime("%m-%d %H:%M:%S"), "nodes": sorted({x[1] for x in cur})})
    res["network"] = {
        "hardware": {n: nodes[n]["identity"]["hw_name"] for n in NODES},
        "ack_ping_attribution": {"acked_prefix_F5CE7_share_pct_of_012_acks": ping_share,
                                 "note": "F5CE7xxx = msg_ids of OE3XWJ-12 ping (`OE3XWJ-12>2025:{ping}`, 1/min, 1529 sent per xwj [PING] lines)",
                                 "ack_airtime_share_pct": ping_air},
        **({"oe3xir_invalid_ping": {"aprs_decode_errors_dest_255": nodes["oe3xir"]["health"]["aprs_decode_errors"],
                                    "note": "689 ring entries `255.:{ping}` (phone_msg, prio1/3A) read every 60 s and discarded at TX decode (CAD_FREE_NO_TX); no airtime"}}
           if "oe3xir" in nodes else {}),
        "common_beat_miss_events_oe3": common,
        "noise_carpet_p5_dbm": {n: nodes[n]["rf"]["rx_error_rssi"].get("p5") for n in NODES},
        "pre_cad_events_pct": {n: nodes[n]["firmware_census"]["events_pre_cad_pct"] for n in NODES},
        "chan_util_mean_pct": {n: nodes[n]["channel"]["CHANNEL_UTIL"].get("mean") for n in NODES},
        "clock": {n: nodes[n]["clock"].get("regimes") for n in NODES},
    }
    (hd / "results.json").write_text(json.dumps(res, indent=1, default=str))

    # ------------------------------------------------------------------ findings.md
    if set(NODES) != NARRATIVE_NODES:
        generic = ["# 24-h log baseline per node\n",
                   "The narrative verdicts are written for one specific capture set; for this node set only the "
                   "machine-readable `results.json` is produced.\n",
                   "| Node | Callsign | Span h |\n|---|---|---|"]
        for n in NODES:
            generic.append(f"| {n} | {nodes[n]['identity']['callsign']} | {nodes[n]['span']['hours']} |")
        (hd / "findings.md").write_text("\n".join(generic) + "\n")
        return
    md: list[str] = []
    md.append("# 24-h log baseline per node (captures 2026-10-30/31 -> 11-01)\n")
    md.append("Facts are counted from the logs by `scripts/node_baseline.py` (+ helper scripts); inferences are marked. Status words: OK / Beobachten / Handlungsbedarf "
              "(thresholds are in `scripts/build_report.py:verdicts`, mine, not firmware limits). Times are logger clock (CET = UTC+1; NTP epoch comparison 3601 +-1 s); "
              "DB0AIS times are derived from node millis + PuTTY start (+-5 s). `[LOG]` receptions are printed before dedup, so they are receptions, not accepted packets.\n")
    md.append("## Overview\n")
    md.append("| Node | HW | Span h | Util mean/peak % | Noise carpet p5 (dBm) | RX_ERROR share | CAD >=3 attempts | Pre-CAD events % | BEAT miss % |\n|---|---|---|---|---|---|---|---|---|")
    for n in NODES:
        r = nodes[n]
        md.append(f"| {r['identity']['callsign']} | {r['identity']['hw_name']} | {r['span']['hours']} | {r['channel']['CHANNEL_UTIL']['mean']}/{r['channel']['CHANNEL_UTIL']['max']:.0f} | "
                  f"{r['rf']['rx_error_rssi']['p5']:.0f} | {r['rf']['rx_error_share_of_rx_pct']}% | {r['cad']['attempt_ge3_pct']}% | {r['firmware_census']['events_pre_cad_pct']}% | {r['gateway']['beat_miss_pct_of_keepalives']}% |")
    md.append("")
    md.append("Cross-node facts:\n")
    md.append("- All six nodes are RAK4631 (nRF52840 + W5100S Ethernet), Internet gateways on build stamp 20260929, all booted within minutes before logging started (first STAT up=300 s, boot estimates in each Crashes row), none rebooted during the capture.")
    md.append(f"- ACK traffic: on the five OE3 nodes {min(ping_share.values())}-{max(ping_share.values())}% of all 012-byte ACK receptions acknowledge msg_ids F5CE7xxx = the 1/min ping of OE3XWJ-12 (`OE3XWJ-12>2025:{{ping}}`). "
              f"ACK frames are {min(v for k, v in ping_air.items() if k in OE3):.0f}-{max(v for k, v in ping_air.items() if k in OE3):.0f}% of decoded airtime on the OE3 nodes (DB0AIS: {ping_air['db0ais']}%). Verdict: Handlungsbedarf unless the 1/min ping is an intended test (operator question).")
    md.append("- OE3XIR-12 emits one invalid ping per minute (`255.:{ping}`, 689x): firmware reads it from the TX ring and drops it at decode (`APRS decode - Destination-CallSign Error [255.]`, `CAD_FREE_NO_TX`). No airtime, but it burns a CAD cycle and a ring slot per minute. Verdict: Beobachten, suspected input-validation gap (see OE3XIR section).")
    md.append(f"- Common server-side event: all five OE3 gateways lost BEAT replies at {common[0]['start'] if common else 'n/a'} (episodes starting within 75 s on {len(common[0]['nodes']) if common else 0} nodes) -> OE server/uplink, not a node fault.")
    xg = nodes["oe3xia"]["rx_silence"]
    md.append(f"- OE3XIA-12 receive silences: {xg['gaps_gt_300s']} periods >5 min without a single OnRxDone/OnRxError ({xg['silent_seconds_in_gaps_gt_300s']} s = {xg['silent_pct_of_span']}% of the capture, longest {xg['longest_gaps'][0]['gap_s']:.0f} s at {xg['longest_gaps'][0]['start']}) while every other OE3 node still logging kept receiving (16-309 events per window; OE3XIR's log ends 18:16); no other node has a silence >153 s. Verdict: Handlungsbedarf (see OE3XIA verdict row).")
    md.append("- Node clock vs logger: OE3XHL/XIA/XOC/XWJ +0 h; OE3XIR +1 h (whole capture); DB0AIS +1 h for the first 79 min, then a -1 h step at ~18:20:3x (no cause in the log). Suspect area: TZ-01 re-derivation in `src/clock.cpp:153-165` (`tzApplyNow()`), node_tz setting not in the capture. Verdict: Beobachten.")
    md.append("- {CET} beacons: median interval 607 s (OE3 server OE1XAR-33; DB0AIS' DL gateway receives OE1XAR-62 at 602 s). The beacon text carries UTC (stamp 05:51:20 received at logger 06:51:40) although tagged {CET}: fact from OE3XHL line `2026-10-31 06:51:41.167`. Verdict: Beobachten (naming/semantics, check with the time-signal owner).\n")
    for n in NODES:
        extra = {"strong_signals": strong[n], "time_signal": cet[n]}
        md.extend(node_section(n, nodes[n], extra))
        if n == "oe3xir":
            r = nodes[n]
            ss = strong[n]
            md.append("### OE3XIR extras\n")
            md.append(f"- Capture ends 2026-10-31 18:16:30 (logger stop; node healthy until then: last STAT uptime {r['stat']['up_last']} s).")
            md.append(f"- Interferer evidence: RX_ERROR above -60 dBm {ss['err_strong']} (levels cluster at -50/-51 and -55/-56 dBm, SNR -22..-13 dB), plus {ss['decoded_strong']} decoded frames with inflated RSSI > -60 dBm (SNR median {ss['snr_decoded_strong_median']} dB), "
                      f"{sum(ss['strong_err_per_hour'].values())} events spread over 13 logger hours (21-35/h in full hours). No other node of the six has a single RX_ERROR above -60 dBm. Fact: persistent strong in-band source at the OE3XIR site. Inference: not LoRa (no freq-error info on nRF52 to confirm).")
            md.append(f"- Invalid `255.:{{ping}}`: {r['health']['aprs_decode_errors'].get('APRS_decode_err:APRS decode - Destination-CallSign Error [N.]')} events, exactly 60/h, ring source `phone_msg:3A` {r['ring']['ring_write_by_src_type'].get('phone_msg:3A')}. Evidence: logger 06:47:33.947 and following minutes in oe3xir.log.")
            md.append("")
        if n == "oe3xwj":
            md.append("### OE3XWJ extras\n")
            md.append(f"- OE3XWJ-12 sends a ping DM every 60 s (`[PING]...send Ping to 2025`, {nodes[n]['health']['ping_send_lines']} lines, ring source `phone_msg:3A` {nodes[n]['ring']['ring_write_by_src_type'].get('phone_msg:3A')}, prio 2). "
                      f"Its msg_ids F5CE7xxx account for {ack[n]['top_acked_prefix'][0]['share_pct']}% of ACK receptions at this node and 97-99% at the other OE3 nodes ({ack[n]['top_acked_prefix'][0]['events']} receptions, {ack[n]['top_acked_prefix'][0]['distinct_ack_ids']} distinct ACK ids for 1529 pings = ~2.8 ACKs per ping, each heard ~13x).")
            md.append(f"- ETH link flaps: {nodes[n]['eth']['counters_final']['downs']} (1-4 s each, IP kept; no DHCP events or got_ip at all, inference: static addressing); all 9 flaps fall between 10-31 18:18 and 11-01 05:33 (none in the first 12 h); BEAT misses do not follow them (2 of 9 flaps have a miss within -90..+180 s, both before the flap; flap times in results.json eth.events_first).")
            md.append("")
    # DB0AIS ETH-04 block
    e = eth04["db0ais"]
    r = nodes["db0ais"]
    md.append("## DB0AIS-12 ETH-04 focus (RAK4631 + W5100S, Hamnet 44.149.189.136, server 44.148.230.197, path `hamnet`, server tag DL)\n")
    md.append("Context: ETH-04 = DHCP lease lost for hours on this node (MikroTik, lease 1 w, always-broadcast=no). Fix on fork-dev (F1 reopen UDP keeping IP, F2 W5100S hw reset after 2 DHCP fails, F3 RNG seed from SoftDevice). "
              "Inference: this capture runs a build WITH the fix - the boot line `[ETH];event;rng_seed;src;sd` (src/nrf52/nrf52_main.cpp:512) is the F3 marker. STAT still says build stamp 20260929 (not bumped).\n")
    md.append("| Question | Answer | Evidence |\n|---|---|---|")
    md.append("| Boot / DHCP | OK: link up, DHCP lease obtained, `Ethernet.localIP()` 44.149.189.136, `got_ip` at uptime 20.1 s; `dhcp_begin` blocked the main loop 4171 ms once (boot), `hw_init` 199 ms | db0ais.log lines 42-56, 17:01:11-17:01:31 |")
    md.append(f"| DHCP / ETH stall in 25.2 h | No DHCP stall. got_ip_n stays 1, renews {r['eth']['counters_final']['renews']}, renew_fail {r['eth']['counters_final']['renew_fail']}, W5100S hw resets {r['eth']['counters_final']['resets']}, `reopen_udp`/`hw_reset` events 0. Only blocking calls after boot: ntp 202 ms (01:01:32) and 404 ms (01:16:29), udp_tx 613 ms (01:31:34), all inside the night disturbance window | `[ETH];link;up` 60-s summaries ({r['eth']['summaries']}), `[ETH];stall` lines |")
    md.append("| Link flaps | 2 (carrier loss 1.0 s at 10-30 22:14:57 and 2.0 s at 10-31 02:20:35); IP kept in both (`ip;1`), no DHCP re-run, UDP kept working | `[ETH];event;link;down/up` ms 18826775/18827782 and 33564381/33566397 |")
    w1, w2 = e["link_windows"][0], e["link_windows"][1]
    md.append(f"| Gateway recovery | Yes, automatic, no reboot. Flap 1: last BEAT 22:14:44, silence {w1['first_beat_after_up_s']:.0f} s after link-up (2 keepalives unanswered, BEAT back 22:16:14). "
              f"Flap 2: server silent from 02:18:09 (145.6 s before the flap), first BEAT {w2['first_beat_after_up_s']} s after link-up (02:20:40) -> the path recovered exactly with the flap (inference: upstream switch/router event, not the node) | db0ais_eth.json link_windows |")
    md.append(f"| Internet (Hamnet) path loss | Yes, intermittent: {e['keepalives_without_beat']} of {e['keepalives']} keepalives ({e['pct_without_beat']}%) got no BEAT, '[UDP] Server not responding for 35s - ETH UP' {e['udp_not_responding_n']}x, hb_age_s>35 in {e['hb_age_gt35_summaries']} of {e['eth_summaries']} minute summaries; "
              f"71 of the 77 misses fall between 22:00 and 02:59 (hourly: 22h 7, 23h 10, 00h 11, 01h 23, 02h 20), 6 isolated misses 03:00-18:00; longest silence 121 s (02:18:39-02:20:40). Internet-side {{CET}} downlink missed 5 beacons (gaps 1204 s after 22:14:56, 1807 s after 00:55:31 and 01:55:44) | db0ais_eth.json hourly, cet_beacons.json |")
    md.append(f"| Gateway function during loss | Local TX path intact: keepalive tx never failed (tx_fail {e['tx_fail_final']}, max gap {e['keepalive_gap_max_s']} s), LoRa->server uploads (GWU {e['gwu_n']}) and EXT UDP out ({e['ext_tx_n']}) continued, largest GWU gap {e['gwu_largest_gaps_s'][-1][0]:.0f} s, largest EXT gap {e['ext_tx_largest_gaps_s'][-1][0]:.0f} s (traffic-driven). Whether the server received them is unknowable from the node (no ACK on uploads) | db0ais_eth.json |")
    md.append(f"| [GW];srv / EXT / KEEP / NTP | `[GW];srv;DL;host;44.148.230.197;path;hamnet;ms;20114` (single line at boot); `[EXT]` listening 44.149.189.136:1799 -> 44.149.189.135:1799; `[KEEP]` D69A080A DB0AIS-12 4.40a every 30 s ({e['keepalives']}); NTP ok {r['gateway']['ntp']['ok']}, timeouts 2 (22:16:32 = 1.5 min after flap 1, 22:31:25), OWN-TX-CHECK-UDP {r['gateway']['own_tx_check_udp']} (check:-1 each) | db0ais.log |")
    md.append("| Reboots | 1 (capture starts at boot, RESETREAS=0x4 soft reset, 17:01:11); none afterwards | `[BOOT]` lines, STAT up monotonic |")
    md.append("| What this capture does NOT test | DHCP lease renewal: renews = 0 in 25.2 h (lease 1 w -> T1 at ~3.5 d), so the original ETH-04 trigger (renewal / unicast OFFER to the known IP) never occurred; F2 (hw reset) never fired. OE3 gateways with short leases do renew (OE3XOC every 5 min, OE3XHL every 15 min, `[ETH];event;dhcp;rc;2`, 64 ms main-loop stall each, 0 fails) - they sit on a different network (44.143.x, lease length not logged) | `[ETH];event;dhcp` in oe3xoc/oe3xhl logs |")
    md.append("")
    md.append("Verdict ETH-04: **Beobachten, not decisive.** Boot DHCP and both link flaps are handled cleanly by the fixed build (IP retained, no DHCP stall, no reboot, recovery without intervention). "
              "The DHCP-renewal path that caused ETH-04 was not exercised in this window; the night-time server silence is a path problem upstream of the node (flap 2 recovery coincides with the carrier drop), not an ETH-stack stall. "
              "Next evidence needed: a capture spanning a lease renewal (>3.5 d or a shortened lease), or `--ethdrop` on an INSTRUMENT build.\n")
    md.append("### Method notes and caveats\n")
    md.append("- Noise floor: RSSI on this firmware behaves like total in-band power (weak frames at SNR -19 read the carpet, e.g. OE3XHL: 718 RX_ERROR at exactly -113 dBm, none below). RSSI-SNR therefore overstates the floor for SNR<0 frames; the lower edge of the RX_ERROR RSSI distribution (p5) is used as the carpet, RSSI-SNR (SNR>=0) is shown beside it. Estimators disagree by 5-10 dB; treat absolute values as inference.")
    md.append("- Channel util counts decoded frames + own TX only (STAT rx = ToA sum of decoded frames within ~1%); RX_ERROR energy is not included, so the real busy fraction is higher than the util figure.")
    md.append("- ESP32 util-inflation caveat does not apply (all RAK4631); fingerprint test (rx= multiples of 2476 ms) negative on all six.")
    md.append("- 'Unique originators' use path[0]; for the logging node's own call this is an echo of own frames (excluded from the firmware census). Last-seen FW per call is replaced by the most frequent non-`#` value.")
    md.append("- Master data (hardware/place/distance) from the `--master` directory (`master.tsv`, `distances.tsv`); OE3XIR has no own distances (position absent), `n/a` there.")
    md.append("- OE3XIR log stops 2026-10-31 18:16:30 (logger), so its percentages cover 11.5 h.")
    md.append("- Console interleaving: a handful of lines are spliced from two sources (e.g. `ONRXDONE_TIME ms=7000C880...`); regexes are anchored so they are dropped, not mis-counted.")
    (hd / "findings.md").write_text("\n".join(md) + "\n")


if __name__ == "__main__":
    main()
