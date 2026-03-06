#!/usr/bin/env bash
# MeshCom Log-Analyse Tool
# Extracts all analysis data from a MeshCom serial log in a single pass.
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

# Extract callsign, RSSI, SNR, HW, FW, hop from MH-LoRa lines
# Output: one line per MH-LoRa occurrence with key fields
grep "MH-LoRa:" "$LOGFILE" | awk '{
    # Extract timestamp (first field)
    ts = $1
    # Find the MH-LoRa: content
    idx = index($0, "MH-LoRa:")
    rest = substr($0, idx + 8)
    # Print timestamp + rest for downstream processing
    print ts " " rest
}' | head -50000 > /tmp/mh_lora_raw.txt

# Per-callsign summary
# Extract callsign (first token after MH-LoRa:), count, RSSI range
awk '{
    # rest starts after timestamp
    # typical: OE1XYZ-15 ... R:-105 S:-5 ... H03 HW:17 FW:44:5
    split($0, a, " ")
    # callsign is a[2] (after timestamp)
    cs = a[2]
    if (cs == "") next

    count[cs]++

    # Find RSSI (R: field)
    for (i=1; i<=NF; i++) {
        if ($i ~ /^R:/) {
            val = substr($i, 3) + 0
            if (!(cs in rssi_min) || val < rssi_min[cs]) rssi_min[cs] = val
            if (!(cs in rssi_max) || val > rssi_max[cs]) rssi_max[cs] = val
        }
        if ($i ~ /^S:/) {
            val = substr($i, 3) + 0
            if (!(cs in snr_min) || val < snr_min[cs]) snr_min[cs] = val
            if (!(cs in snr_max) || val > snr_max[cs]) snr_max[cs] = val
            snr_sum[cs] += val
            snr_cnt[cs]++
        }
        if ($i ~ /^H[0-9][0-9]$/) {
            h = $i
            if (!(cs in hop_vals)) hop_vals[cs] = h
            else if (index(hop_vals[cs], h) == 0) hop_vals[cs] = hop_vals[cs] "," h
        }
        if ($i ~ /^HW:/) {
            hw[cs] = $i
        }
        if ($i ~ /^FW:/) {
            fw[cs] = $i
        }
    }

    # First/last seen
    ts = $1
    if (!(cs in first_seen)) first_seen[cs] = ts
    last_seen[cs] = ts
}
END {
    # Sort by count descending
    n = asorti(count, sorted)
    # Print header
    printf "%-15s %6s %12s %10s %8s %8s %8s %s\n", "CALLSIGN", "COUNT", "RSSI", "SNR_AVG", "HOPS", "HW", "FW", "FIRST_SEEN"
    # Collect into array for sorting
    for (i=1; i<=n; i++) {
        cs = sorted[i]
        savg = (snr_cnt[cs] > 0) ? sprintf("%.1f", snr_sum[cs]/snr_cnt[cs]) : "?"
        printf "%-15s %6d %4d..%4d %10s %8s %8s %8s %s\n", \
            cs, count[cs], rssi_min[cs], rssi_max[cs], savg, \
            hop_vals[cs], (cs in hw ? hw[cs] : "?"), (cs in fw ? fw[cs] : "?"), first_seen[cs]
    }
}' /tmp/mh_lora_raw.txt | sort -t' ' -k2 -rn

echo ""
echo "UNIQUE_NODES: $(awk '{print $2}' /tmp/mh_lora_raw.txt | sort -u | wc -l | tr -d ' ')"
echo "TOTAL_MH_PACKETS: $(wc -l < /tmp/mh_lora_raw.txt)"

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
grep "RX-LoRa:" "$LOGFILE" | grep -v 'H@R\|HG@R\|\*!' | head -20 || true

# ─── 4. HOP DISTRIBUTION ───
section "HOP_DISTRIBUTION"

echo "--- MH-LoRa (Empfangen) ---"
grep "MH-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

echo "--- RX-LoRa (Akzeptiert) ---"
grep "RX-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

echo "--- TX-LoRa (Gesendet) ---"
grep "TX-LoRa:" "$LOGFILE" | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

# ─── 5. LOOPS ───
section "LOOPS"

echo "LOOP_COUNT: $(grep -ci 'loop' "$LOGFILE" 2>/dev/null || echo 0)"
grep -i "loop" "$LOGFILE" | head -10 || true

# ─── 6. CHANNEL UTILIZATION ───
section "CHANNEL_UTIL"

grep "CHANNEL_UTIL" "$LOGFILE" | awk '{
    # Extract timestamp and utilization value
    ts = $1
    # Find rx= value
    for (i=1; i<=NF; i++) {
        if ($i ~ /^rx=/) {
            val = substr($i, 4) + 0.0
            print ts, val
        }
    }
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
        # Split into first/second half
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
        # Assume timestamp format like HH:MM:SS or similar
        split($1, t, ":")
        # Group by 10-min intervals
        bucket = t[1] ":" sprintf("%02d", int(t[2]/10)*10)
        sum[bucket] += $2
        cnt[bucket]++
        if (!(bucket in mx) || $2 > mx[bucket]) mx[bucket] = $2
    }
    END {
        n = asorti(sum, sorted)
        printf "%-8s %8s %8s %5s\n", "TIME", "AVG%", "MAX%", "N"
        for (i=1; i<=n; i++) {
            b = sorted[i]
            printf "%-8s %8.1f %8.1f %5d\n", b, sum[b]/cnt[b], mx[b], cnt[b]
        }
    }' /tmp/channel_util.txt
else
    echo "NO_DATA"
fi

# ─── 7. ACK ANALYSIS ───
section "ACK_ANALYSIS"

echo "NODE_ACK_QUEUED: $(grep -c 'NODE_ACK_QUEUED' "$LOGFILE" 2>/dev/null || echo 0)"
echo "ACK_FAST_QUEUED: $(grep -c 'ACK_FAST_QUEUED' "$LOGFILE" 2>/dev/null || echo 0)"
echo "ACK_FAST_TX: $(grep -c 'ACK_FAST_TX' "$LOGFILE" 2>/dev/null || echo 0)"
echo "TX_ACK_FAST: $(grep -c 'TX-ACK-Fast' "$LOGFILE" 2>/dev/null || echo 0)"

# ACK storm check: ACKs per original message ID
echo ""
echo "--- ACK_PER_MSGID (top 10) ---"
grep "TX-ACK-Fast" "$LOGFILE" | awk -F'TX-ACK-Fast:' '{print $2}' | awk '{print $4}' | sort | uniq -c | sort -rn | head -10 || true

# ACK distribution
echo ""
echo "--- ACK_DISTRIBUTION ---"
grep "TX-ACK-Fast" "$LOGFILE" | awk -F'TX-ACK-Fast:' '{print $2}' | awk '{print $4}' | sort | uniq -c | awk '{print $1}' | sort | uniq -c | sort -rn || true

# ACK queue length
echo ""
echo "--- ACK_QLEN ---"
grep -oE 'ack_qlen=[0-9]+' "$LOGFILE" | sort | uniq -c | sort -rn || true

# ─── 8. CRC ERRORS ───
section "CRC_ERRORS"

echo "CRC_ERROR_COUNT: $(grep -c 'CRC_ERROR' "$LOGFILE" 2>/dev/null || echo 0)"

grep "CRC_ERROR" "$LOGFILE" | awk '{
    for (i=1; i<=NF; i++) {
        if ($i ~ /^R:/) rssi = $i
        if ($i ~ /^S:/) snr = $i
        if ($i ~ /freq_err/) ferr = $i
        if ($i ~ /^sz=/) sz = $i
    }
    print $1, rssi, snr, ferr, sz
}' | head -50 || true

# Classify by freq error
echo ""
echo "--- CRC_FREQ_CLASSIFICATION ---"
grep "CRC_ERROR" "$LOGFILE" | grep -oE 'freq_err=[0-9.-]+' | awk -F= '{
    v = ($2 < 0) ? -$2 : $2
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

echo "RING_STATUS_COUNT: $(grep -c 'RING_STATUS' "$LOGFILE" 2>/dev/null || echo 0)"

echo "--- RETRYING ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'retrying=[0-9]+' | sort | uniq -c | sort -rn || true

echo "--- PENDING ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'pending=[0-9]+' | sort | uniq -c | sort -rn || true

echo "--- QUEUED ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'queued=[0-9]+' | sort | uniq -c | sort -rn || true

# ─── 10. MISSING ACKS ───
section "MISSING_ACKS"

echo "ACK_TIMEOUT: $(grep -ci 'ACK_TIMEOUT' "$LOGFILE" 2>/dev/null || echo 0)"
echo "ACK_FAIL: $(grep -ci 'ACK_FAIL' "$LOGFILE" 2>/dev/null || echo 0)"
echo "ACK_MISS: $(grep -ci 'ACK_MISS' "$LOGFILE" 2>/dev/null || echo 0)"
echo "ACK_LOST: $(grep -ci 'ACK_LOST' "$LOGFILE" 2>/dev/null || echo 0)"

# ─── 11. DEDUP ───
section "DEDUP"

echo "DEDUP_EXPLICIT: $(grep -ci 'dedup\|DEDUP' "$LOGFILE" 2>/dev/null || echo 0)"

MH_COUNT=$(grep -c "MH-LoRa:" "$LOGFILE" 2>/dev/null || echo 0)
RX_COUNT=$(grep -c "RX-LoRa:" "$LOGFILE" 2>/dev/null || echo 0)
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

echo "MC_SM_TOTAL: $(grep -c 'MC-SM' "$LOGFILE" 2>/dev/null || echo 0)"
echo "MC_SM_ERRORS: $(grep 'MC-SM' "$LOGFILE" | grep -vc 'rc=0' 2>/dev/null || echo 0)"

grep "MC-SM" "$LOGFILE" | grep -v "rc=0" | head -10 || true

# ─── 13. ADDITIONAL CHECKS ───
section "ADDITIONAL"

echo "--- WIFI_ISSUES ---"
grep -iE 'disconnect|reconnect|WIFI.*fail' "$LOGFILE" | head -10 || true
echo "WIFI_ISSUE_COUNT: $(grep -ciE 'disconnect|reconnect|WIFI.*fail' "$LOGFILE" 2>/dev/null || echo 0)"

echo ""
echo "--- CRASHES ---"
grep -iE 'panic|abort|watchdog|wdt|backtrace|guru.meditation' "$LOGFILE" | head -10 || true
echo "CRASH_COUNT: $(grep -ciE 'panic|abort|watchdog|wdt|backtrace|guru.meditation' "$LOGFILE" 2>/dev/null || echo 0)"

echo ""
echo "--- HEAP_TREND ---"
grep '\[HEAP\]' "$LOGFILE" | head -5
echo "..."
grep '\[HEAP\]' "$LOGFILE" | tail -5
HEAP_SAMPLES=$(grep -c '\[HEAP\]' "$LOGFILE" 2>/dev/null || echo 0)
echo "HEAP_SAMPLES: $HEAP_SAMPLES"

echo ""
echo "--- ONRXDONE_TIME ---"
grep "ONRXDONE_TIME" "$LOGFILE" | grep -oE 'ONRXDONE_TIME=[0-9]+' | awk -F= '{
    sum += $2; count++
    if (count == 1 || $2 > max) max = $2
    if (count == 1 || $2 < min) min = $2
}
END {
    if (count > 0) printf "AVG: %.0f ms, MIN: %d, MAX: %d, SAMPLES: %d\n", sum/count, min, max, count
    else print "NO_DATA"
}' || true

echo ""
echo "--- RX_TIMEOUT ---"
echo "RX_TIMEOUT_FIRE: $(grep -c 'RX_TIMEOUT_FIRE' "$LOGFILE" 2>/dev/null || echo 0)"

# ─── DONE ───
section "END"
echo "Analysis complete."

# Cleanup
rm -f /tmp/mh_lora_raw.txt /tmp/channel_util.txt
