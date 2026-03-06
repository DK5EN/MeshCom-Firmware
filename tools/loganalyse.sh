#!/usr/bin/env bash
# MeshCom Log-Analyse Tool
# Extracts all analysis data from a MeshCom serial monitor log in a single pass.
# Expects the log format produced by serial_monitor.py:
#   2026-03-06 15:36:58.248  [firmware output here]
#
# Usage: ./tools/loganalyse.sh <logfile>
#
# Output: Structured sections separated by "=== SECTION_NAME ===" markers
# for easy parsing by the logauswertung skill.

set -euo pipefail

if [ $# -lt 1 ] || [ ! -f "$1" ]; then
    echo "Usage: $0 <logfile>" >&2
    exit 1
fi

LOGFILE="$1"

# ─── Helper ───
section() { echo ""; echo "=== $1 ==="; }

# ─── 1. LOG OVERVIEW ───
section "OVERVIEW"

# Boot info from first 60 lines
head -60 "$LOGFILE" | grep -iE "POWERON_RESET|SW_CPU_RESET|DEEPSLEEP_RESET|boot|rst:" || true

# Node ID
grep -m1 '\[INIT\].*_GW_ID:' "$LOGFILE" || true

# Callsign
grep -m1 '\[BLE \].*Device started with BLE-Name' "$LOGFILE" || true

# LoRa config
grep '\[LoRa\].*RF_' "$LOGFILE" || true

# CSMA
grep -m1 'CSMA_SLOT_TIME' "$LOGFILE" || true

# Heap/PSRAM at boot
grep -m1 '\[HEAP\]' "$LOGFILE" || true
grep -m1 '\[PSRM\]' "$LOGFILE" || true

# WiFi
grep '\[WIFI\]' "$LOGFILE" | head -10 || true

# Hardware (display, sensors)
grep -iE '\[INIT\].*display|\[INIT\].*BME|INA226|SHT|BMP' "$LOGFILE" | head -5 || true

# First and last timestamp
echo "FIRST_LINE: $(head -1 "$LOGFILE")"
echo "LAST_LINE: $(tail -1 "$LOGFILE")"
echo "TOTAL_LINES: $(wc -l < "$LOGFILE")"

# ─── 2. ACTIVE NODES ───
section "NODES"

# Single-pass awk: correlate "Received packet:" RSSI/SNR with subsequent MH-LoRa lines.
# Extract callsign from route field (first callsign before comma or ">").
awk '
/Received packet:/ {
    for (i=1; i<=NF; i++) {
        if ($i == "RSSI:") {
            for (j=i+1; j<=NF; j++) {
                if ($j != "" && $j ~ /^-?[0-9]/) { last_rssi = $j + 0; break }
            }
        }
        if ($i == "SNR:") {
            for (j=i+1; j<=NF; j++) {
                if ($j != "" && $j ~ /^-?[0-9]/) { last_snr = $j + 0; break }
            }
        }
    }
    has_rssi = 1
    next
}

/MH-LoRa:/ {
    # Serial monitor timestamp is $2 (HH:MM:SS.mmm)
    ts = $2

    # Find route field (contains ">")
    cs = ""
    for (i=1; i<=NF; i++) {
        if ($i ~ />/) {
            # Route like "DK7CH-1,DL7OSX-1>*!..."
            # First callsign is before first comma or ">"
            split($i, r, /[,>]/)
            cs = r[1]
            break
        }
    }
    if (cs == "" || cs ~ /^[0-9]/) next

    cnt[cs]++

    # RSSI/SNR from preceding Received packet line
    if (has_rssi) {
        if (!(cs in rssi_min) || last_rssi < rssi_min[cs]) rssi_min[cs] = last_rssi
        if (!(cs in rssi_max) || last_rssi > rssi_max[cs]) rssi_max[cs] = last_rssi
        if (!(cs in snr_min) || last_snr < snr_min[cs]) snr_min[cs] = last_snr
        if (!(cs in snr_max) || last_snr > snr_max[cs]) snr_max[cs] = last_snr
        snr_sum[cs] += last_snr
        snr_cnt[cs]++
        has_rssi = 0
    }

    # Hop count (H01, H02, ...)
    for (i=1; i<=NF; i++) {
        if ($i ~ /^H[0-9][0-9]$/) {
            h = $i
            if (!(cs in hop_vals)) hop_vals[cs] = h
            else if (index(hop_vals[cs], h) == 0) hop_vals[cs] = hop_vals[cs] "," h
            break
        }
    }

    # HW and FW
    for (i=1; i<=NF; i++) {
        if ($i ~ /^HW:/) hw[cs] = $i
        if ($i ~ /^FW:/) fw[cs] = $i
    }

    # First/last seen (serial monitor time)
    if (!(cs in first_seen)) first_seen[cs] = ts
    last_seen[cs] = ts
}
END {
    for (cs in cnt) {
        savg = (snr_cnt[cs] > 0) ? sprintf("%.1f", snr_sum[cs]/snr_cnt[cs]) : "?"
        if (cs in rssi_min)
            rssi_str = sprintf("%4d..%4d", rssi_min[cs], rssi_max[cs])
        else
            rssi_str = "       n/a"
        printf "%-15s %6d %12s %10s %8s %8s %8s %s\n", \
            cs, cnt[cs], rssi_str, savg, \
            (cs in hop_vals ? hop_vals[cs] : "?"), \
            (cs in hw ? hw[cs] : "?"), \
            (cs in fw ? fw[cs] : "?"), \
            first_seen[cs]
    }
}' "$LOGFILE" | (
    printf "%-15s %6s %12s %10s %8s %8s %8s %s\n" "CALLSIGN" "COUNT" "RSSI" "SNR_AVG" "HOPS" "HW" "FW" "FIRST_SEEN"
    sort -t' ' -k2 -rn
)

echo ""
echo "UNIQUE_NODES: $(grep "MH-LoRa:" "$LOGFILE" | awk '{for(i=1;i<=NF;i++){if($i~/>/){{split($i,r,/[,>]/);if(r[1]!=""&&r[1]!~/^[0-9]/)print r[1];break}}}}' | sort -u | wc -l | tr -d ' ')"
echo "TOTAL_MH_PACKETS: $(grep -c 'MH-LoRa:' "$LOGFILE")"

# ─── 3. MESSAGE TYPES ───
section "MESSAGE_TYPES"

grep "MH-LoRa:" "$LOGFILE" | awk '
{
    if (/H@R/ || /HG@R/) heartbeat++
    else if (/\*!/) position++
    else if (/:/) text++
    else other++
    total++
}
END {
    printf "HEARTBEAT: %d (%.1f%%)\n", heartbeat, (total>0 ? heartbeat*100.0/total : 0)
    printf "POSITION: %d (%.1f%%)\n", position, (total>0 ? position*100.0/total : 0)
    printf "TEXT: %d (%.1f%%)\n", text, (total>0 ? text*100.0/total : 0)
    printf "OTHER: %d (%.1f%%)\n", other, (total>0 ? other*100.0/total : 0)
    printf "TOTAL: %d\n", total
}' || true

# Notable text messages (extract up to 20)
echo ""
echo "--- NOTABLE_TEXTS ---"
grep "RX-LoRa-All:" "$LOGFILE" | grep -v 'H@R\|HG@R\|\*!' | head -20 || true

# ─── 4. HOP DISTRIBUTION ───
section "HOP_DISTRIBUTION"

echo "--- MH-LoRa (Empfangen) ---"
grep "MH-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

echo "--- RX-LoRa2 (Akzeptiert) ---"
grep "RX-LoRa2:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

echo "--- TX-LoRa (Gesendet) ---"
grep "TX-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

# ─── 5. LOOPS ───
section "LOOPS"

echo "RELAY_LOOP_BLOCKED: $(grep -c 'RELAY_LOOP_BLOCKED' "$LOGFILE" 2>/dev/null; true)"
grep "RELAY_LOOP_BLOCKED" "$LOGFILE" | head -10 || true

# ─── 6. CHANNEL UTILIZATION ───
section "CHANNEL_UTIL"

{ grep "CHANNEL_UTIL" "$LOGFILE" || true; } | awk '{
    # Serial monitor timestamp is $2 (HH:MM:SS.mmm)
    ts = $2
    for (i=1; i<=NF; i++) {
        if ($i ~ /^rx=/) {
            val = substr($i, 4) + 0.0
        }
        if ($i ~ /^util=/) {
            util = substr($i, 6) + 0.0
        }
    }
    # Use util% as the main metric
    if (util != "") print ts, util
}' > /tmp/channel_util.txt

if [ -s /tmp/channel_util.txt ]; then
    awk '
    {
        val = $2 + 0.0
        sum += val
        count++
        if (count == 1 || val > max) max = val
        if (count == 1 || val < min) min = val
        if (val >= 70) high70++
        if (val >= 90) high90++
        vals[count] = val
    }
    END {
        avg = sum / count
        half = int(count / 2)
        sum1 = 0; sum2 = 0
        for (i=1; i<=half; i++) sum1 += vals[i]
        for (i=half+1; i<=count; i++) sum2 += vals[i]
        avg1 = (half > 0) ? sum1 / half : 0
        avg2 = (count - half > 0) ? sum2 / (count - half) : 0
        printf "SAMPLES: %d\n", count
        printf "AVG: %.1f%%\n", avg
        printf "MIN: %.1f%%\n", min
        printf "MAX: %.1f%%\n", max
        printf "FIRST_HALF_AVG: %.1f%%\n", avg1
        printf "SECOND_HALF_AVG: %.1f%%\n", avg2
        printf "TREND: %s\n", (avg2 > avg1 * 1.1) ? "STEIGEND" : (avg1 > avg2 * 1.1) ? "FALLEND" : "STABIL"
        printf "SAMPLES_GE_70: %d\n", high70
        printf "SAMPLES_GE_90: %d\n", high90
    }' /tmp/channel_util.txt

    # Per 10-minute buckets
    echo ""
    echo "--- BUCKETS_10MIN ---"
    awk '{
        # Timestamp is HH:MM:SS.mmm — split by ":" for hour:minute
        split($1, t, ":")
        bucket = t[1] ":" sprintf("%02d", int(t[2]/10)*10)
        sum[bucket] += $2
        cnt[bucket]++
        if (!(bucket in mx) || $2 > mx[bucket]) mx[bucket] = $2
    }
    END {
        printf "%-8s %8s %8s %5s\n", "TIME", "AVG%", "MAX%", "N"
        for (b in sum) {
            printf "%-8s %8.1f %8.1f %5d\n", b, sum[b]/cnt[b], mx[b], cnt[b]
        }
    }' /tmp/channel_util.txt | sort
else
    echo "NO_DATA"
fi

# ─── 7. ACK ANALYSIS ───
section "ACK_ANALYSIS"

echo "ACK_FAST_QUEUED: $(grep -c 'ACK_FAST_QUEUED' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_FAST_TX: $(grep -c 'ACK_FAST_TX' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_RX_CANCEL: $(grep -c 'ACK_RX_CANCEL' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_FWD_DEDUP: $(grep -c 'ACK_FWD_DEDUP' "$LOGFILE" 2>/dev/null; true)"
echo "GW_ACK_DEDUP: $(grep -c 'GW_ACK_DEDUP' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_SLOT_SKIP: $(grep -c 'ACK_SLOT_SKIP' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_FWD_DROPPED: $(grep -c 'ACK_FWD_DROPPED' "$LOGFILE" 2>/dev/null; true)"
echo "GW_ACK_DROPPED: $(grep -c 'GW_ACK_DROPPED' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_CANCEL_RETRANSMIT: $(grep -c 'ACK_CANCEL_RETRANSMIT' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_RECEIVED: $(grep -c 'ACK_RECEIVED' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_FAST_CAD_BUSY: $(grep -c 'ACK_FAST_CAD_BUSY' "$LOGFILE" 2>/dev/null; true)"

# ACK queue length distribution
echo ""
echo "--- ACK_QLEN ---"
grep -oE 'ack_qlen=[0-9]+' "$LOGFILE" | sort | uniq -c | sort -rn || true

# ─── 8. CRC ERRORS ───
section "CRC_ERRORS"

echo "CRC_ERROR_COUNT: $(grep -c 'CRC_ERROR' "$LOGFILE" 2>/dev/null; true)"

grep "CRC_ERROR" "$LOGFILE" | awk '{
    for (i=1; i<=NF; i++) {
        if ($i ~ /^rssi=/) rssi = $i
        if ($i ~ /^snr=/) snr = $i
        if ($i ~ /^freq_err=/) ferr = $i
        if ($i ~ /^size=/) sz = $i
    }
    print $2, rssi, snr, ferr, sz
}' | head -50 || true

# Classify by freq error
echo ""
echo "--- CRC_FREQ_CLASSIFICATION ---"
grep "CRC_ERROR" "$LOGFILE" | grep -oE 'freq_err=[0-9.-]+' | awk -F= '{
    v = $2 + 0; if (v < 0) v = -v
    if (v > 3000) offfreq++
    else if (v > 1000) medium++
    else collision++
}
END {
    printf "OFF_FREQUENCY (>3kHz): %d\n", offfreq+0
    printf "MEDIUM (1-3kHz): %d\n", medium+0
    printf "COLLISION (<1kHz): %d\n", collision+0
}' || true

# ─── 9. RETRIES (RING_STATUS) ───
section "RING_STATUS"

echo "RING_STATUS_COUNT: $(grep -c 'RING_STATUS' "$LOGFILE" 2>/dev/null; true)"

echo "--- RETRYING ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'retrying=[0-9]+' | sort | uniq -c | sort -rn || true

echo "--- PENDING ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'pending=[0-9]+' | sort | uniq -c | sort -rn || true

echo "--- QUEUED ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'queued=[0-9]+' | sort | uniq -c | sort -rn || true

# ─── 10. MISSING ACKS ───
section "MISSING_ACKS"

echo "ACK_TIMEOUT: $(grep -ci 'ACK_TIMEOUT' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_FAIL: $(grep -ci 'ACK_FAIL' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_MISS: $(grep -ci 'ACK_MISS' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_LOST: $(grep -ci 'ACK_LOST' "$LOGFILE" 2>/dev/null; true)"
echo "RETRANSMIT_GIVEUP: $(grep -c 'RETRANSMIT_GIVEUP' "$LOGFILE" 2>/dev/null; true)"

# ─── 11. DEDUP ───
section "DEDUP"

echo "DEDUP_EXPLICIT: $(grep -ci 'dedup\|DEDUP' "$LOGFILE" 2>/dev/null; true)"

MH_COUNT=$(grep -c "MH-LoRa:" "$LOGFILE" 2>/dev/null; true)
RX_COUNT=$(grep -c "RX-LoRa2:" "$LOGFILE" 2>/dev/null; true)
echo "MH_LORA_HEARD: $MH_COUNT"
echo "RX_LORA_ACCEPTED: $RX_COUNT"
echo "IMPLICIT_DEDUP: $((MH_COUNT - RX_COUNT))"

# Top repeated msg_ids
echo ""
echo "--- TOP_REPEATED_MSGIDS ---"
grep "MH-LoRa:" "$LOGFILE" | grep -oE 'x[A-F0-9]{8}' | sort | uniq -c | sort -rn | head -10 || true

# Max duplication factor
echo ""
echo "--- DUPLICATION_DISTRIBUTION ---"
grep "MH-LoRa:" "$LOGFILE" | grep -oE 'x[A-F0-9]{8}' | sort | uniq -c | awk '{print $1}' | sort -n | uniq -c | sort -rn || true

# ─── 12. STATE MACHINE ───
section "STATE_MACHINE"

echo "MC_SM_TOTAL: $(grep -c 'MC-SM' "$LOGFILE" 2>/dev/null; true)"
echo "MC_SM_ERRORS: $(grep 'MC-SM' "$LOGFILE" | grep -vc 'rc=0' 2>/dev/null; true)"

grep "MC-SM" "$LOGFILE" | grep -v "rc=0" | head -10 || true

# ─── 13. ADDITIONAL CHECKS ───
section "ADDITIONAL"

echo "--- WIFI_ISSUES ---"
grep -iE 'disconnect|reconnect|WIFI.*fail' "$LOGFILE" | head -10 || true
echo "WIFI_ISSUE_COUNT: $(grep -ciE 'disconnect|reconnect|WIFI.*fail' "$LOGFILE" 2>/dev/null; true)"

echo ""
echo "--- CRASHES ---"
grep -iE 'panic|abort|watchdog|wdt|backtrace|guru.meditation' "$LOGFILE" | head -10 || true
echo "CRASH_COUNT: $(grep -ciE 'panic|abort|watchdog|wdt|backtrace|guru.meditation' "$LOGFILE" 2>/dev/null; true)"

echo ""
echo "--- HEAP_TREND ---"
grep '\[HEAP\]' "$LOGFILE" | head -5
echo "..."
grep '\[HEAP\]' "$LOGFILE" | tail -5
HEAP_SAMPLES=$(grep -c '\[HEAP\]' "$LOGFILE" 2>/dev/null; true)
echo "HEAP_SAMPLES: $HEAP_SAMPLES"

echo ""
echo "--- ONRXDONE_TIME ---"
grep "ONRXDONE_TIME" "$LOGFILE" | grep -oE 'ms=[0-9]+' | awk -F= '{
    sum += $2; count++
    if (count == 1 || $2 > max) max = $2
    if (count == 1 || $2 < min) min = $2
}
END {
    if (count > 0) printf "AVG: %.0f ms, MIN: %d, MAX: %d, SAMPLES: %d\n", sum/count, min, max, count
    else print "NO_DATA"
}' || true

echo ""
echo "--- RX_TIMEOUT_FIRE ---"
echo "RX_TIMEOUT_FIRE: $(grep -c 'RX_TIMEOUT_FIRE' "$LOGFILE" 2>/dev/null; true)"
grep "RX_TIMEOUT_FIRE" "$LOGFILE" | grep -oE 'wait=[0-9.]+' | awk -F= '{
    sum += $2; count++
    if (count == 1 || $2 > max) max = $2
    if (count == 1 || $2 < min) min = $2
}
END {
    if (count > 0) printf "ADAPTIVE_WAIT: AVG=%.0f MIN=%.0f MAX=%.0f ms (%d samples)\n", sum/count, min, max, count
    else print "NO_WAIT_DATA"
}' || true

echo ""
echo "--- BUFFER_DROPS ---"
echo "BUFFER_DROPS: $(grep -c 'DROPPED.*buffer_full\|_DROPPED' "$LOGFILE" 2>/dev/null; true)"
grep -E 'DROPPED|buffer_full' "$LOGFILE" | head -10 || true

echo ""
echo "--- CAD_STATS ---"
echo "CAD_GIVEUP: $(grep -c 'CAD_GIVEUP' "$LOGFILE" 2>/dev/null; true)"
echo "CAD_FALSE_POSITIVE: $(grep -c 'CAD_FALSE_POSITIVE' "$LOGFILE" 2>/dev/null; true)"
echo "RX_TIMEOUT_DEFERRED: $(grep -c 'RX_TIMEOUT_DEFERRED' "$LOGFILE" 2>/dev/null; true)"

# ─── DONE ───
section "END"
echo "Analysis complete."

# Cleanup
rm -f /tmp/channel_util.txt
