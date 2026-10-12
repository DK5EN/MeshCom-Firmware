#!/usr/bin/env bash
# MeshCom Log-Analyse Tool
# Extracts all analysis data from a MeshCom serial monitor log in a single pass.
# Expects the log format produced by serial_monitor.py:
#   2026-03-06 15:36:58.248  [firmware output here]
#
# Usage: ./tools/loganalyse.sh <logfile> [<logfile2>]
#        With two logfiles: adds CROSS_CORRELATION section
#
# Output: Structured sections separated by "=== SECTION_NAME ===" markers
# for easy parsing by the logauswertung skill.

set +e  # Do not exit on errors; let sections fail visibly and continue

# Pin the locale: under a decimal-comma locale (de_DE) BSD awk parses "1.5"+0
# as 1 and prints decimal commas, which corrupts every fractional value
# (CRC cluster gaps, break-even %, FERR averages). Also fixes sort order.
export LC_ALL=C
# Lines that carry free packet payload text (chat can contain "reconnect",
# "Guru Meditation", ...): never evidence for a node event. Bracket classes
# instead of backslashes, so the pattern survives `awk -v` escape processing.
PAYLOAD_LINE_RE='(MH|RX|TX)-LoRa2? |RX-UDP |TX-UDP |[[]EXT[]] (Out|Inc):|NEW-TXT:? |[[]LOG[]] [0-9][0-9][0-9] '

if [ $# -lt 1 ] || [ ! -f "$1" ]; then
    echo "Usage: $0 <logfile>" >&2
    exit 1
fi

# ─── Input normalization (TOOL-04) ───
# Firmware serial captures can contain stray non-UTF8 bytes (RF/UART noise).
# Under a UTF-8 locale these abort awk mid-file ("towc: multibyte conversion
# failure"), silently truncating every awk-based section. normalize_log strips
# input to printable ASCII (plus tab/newline) under LC_ALL=C into a temp file
# and echoes its path. Temp files are removed on exit.
_NORMALIZED_TMPS=""
cleanup_normalized_tmps() { [ -n "$_NORMALIZED_TMPS" ] && rm -f $_NORMALIZED_TMPS; }
trap cleanup_normalized_tmps EXIT

# new_tmp: create a temp file, register it for removal on exit and return the
# path in NEW_TMP. Must be called WITHOUT command substitution: a subshell
# would lose the registration (the trap would never see the file).
new_tmp() {
    NEW_TMP="$(mktemp "${TMPDIR:-/tmp}/loganalyse.XXXXXX")"
    _NORMALIZED_TMPS="$_NORMALIZED_TMPS $NEW_TMP"
}

# normalize_log <file>: result path in NORM_RESULT (no command substitution,
# see new_tmp).
normalize_log() {
    _src="$1"
    new_tmp
    _tmp="$NEW_TMP"
    # TOOL-05: detect the raw firmware timestamp format
    #   [EPOCHDAY.HHh.MMm.SSs.mmm]  and convert to serial_monitor format
    #   YYYY-MM-DD HH:MM:SS.mmm  (epoch day -> date via pure-awk Gregorian
    #   math; no `date` call, so identical on macOS/BSD and Linux/GNU).
    # Byte sanitizing (TOOL-04) runs in both branches.
    if LC_ALL=C grep -qE '^\[[0-9]+\.[0-9]+h\.[0-9]+m\.[0-9]+s\.[0-9]+\]' "$_src"; then
        LC_ALL=C tr -cd '\11\12\40-\176' < "$_src" | LC_ALL=C awk '
        function civil(days,   z, era, doe, yoe, y, doy, mp, d, m) {
            # days since 1970-01-01 -> Y-M-D (proleptic Gregorian, Hinnant)
            z = days + 719468
            era = int((z >= 0 ? z : z - 146096) / 146097)
            doe = z - era * 146097
            yoe = int((doe - int(doe/1460) + int(doe/36524) - int(doe/146096)) / 365)
            y = yoe + era * 400
            doy = doe - (365*yoe + int(yoe/4) - int(yoe/100))
            mp = int((5*doy + 2) / 153)
            d = doy - int((153*mp + 2) / 5) + 1
            m = (mp < 10 ? mp + 3 : mp - 9)
            return sprintf("%04d-%02d-%02d", (m <= 2 ? y + 1 : y), m, d)
        }
        BEGIN { last = "1970-01-01 00:00:00.000" }
        {
            if (match($0, /^\[[0-9]+\.[0-9]+h\.[0-9]+m\.[0-9]+s\.[0-9]+\]/)) {
                hdr = substr($0, 2, RLENGTH - 2)
                rest = substr($0, RLENGTH + 1); sub(/^ /, "", rest)
                split(hdr, a, ".")
                hh = a[2]; sub(/h$/, "", hh)
                mm = a[3]; sub(/m$/, "", mm)
                ss = a[4]; sub(/s$/, "", ss)
                last = civil(a[1] + 0) " " hh ":" mm ":" ss "." a[5]
                print last "  " rest
            } else {
                print last "  " $0
            }
        }' > "$_tmp"
    else
        LC_ALL=C tr -cd '\11\12\40-\176' < "$_src" > "$_tmp"
    fi
    NORM_RESULT="$_tmp"
}

INPUT1="$1"   # names printed in CROSS_CORRELATION (LOGFILE is a temp copy)
normalize_log "$1"
LOGFILE="$NORM_RESULT"

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

# Heap/PSRAM at boot
grep -m1 -E '\[HEAP\][; ]' "$LOGFILE" || true
grep -m1 '\[PSRM\]' "$LOGFILE" || true

# WiFi
grep '\[WIFI\]' "$LOGFILE" | head -10 || true

# Hardware (display, sensors)
grep -iE '\[INIT\].*display|\[INIT\].*BME|INA226|SHT|BMP' "$LOGFILE" | head -5 || true

# First and last timestamp
echo "FIRST_LINE: $(head -1 "$LOGFILE")"
echo "LAST_LINE: $(tail -1 "$LOGFILE")"
echo "TOTAL_LINES: $(wc -l < "$LOGFILE")"

# SETLOG presence (firmware `--setlog on`): "[LOG] " lines with the SL-01..06
# bodies. SL_RX_N = RX lines carrying the RSSI/SNR/DUP/OWN tail.
SL_OTHER_N=$(grep -cE '\[LOG\] (RLY|TX|ERR|STAT|GWI|GWU) ' "$LOGFILE" 2>/dev/null; true)
SL_RX_N=$(grep -cE '\[LOG\] [0-9]{3} .* RSSI:-?[0-9]+ SNR:-?[0-9]+ DUP:' "$LOGFILE" 2>/dev/null; true)
SL_HAVE_RCV=$(grep -c 'Received packet:' "$LOGFILE" 2>/dev/null; true)
SL_N=$((SL_RX_N + SL_OTHER_N))
if [ "$SL_N" -gt 0 ]; then echo "SETLOG: on ($SL_N lines)"; else echo "SETLOG: off"; fi

# Setlog-only capture (`--info off`: no MH-LoRa lines, src/lora_functions.cpp)
# would give an empty node table and 0 packets. The `[LOG] NNN <typ> x<id> ...`
# RX line carries the same body as MH-LoRa, so it serves as the packet line:
# a synthetic `MH-LoRa NNN ...` line (tail RSSI.. stripped) is added right
# after each RX tail line. Only done when the log has no MH-LoRa line at all,
# so one source is ever used and nothing is counted twice. Sections that need
# RX-LoRa2/TX-LoRa (not in this capture) print n/a.
MH_SYNTH=0
if [ "$SL_RX_N" -gt 0 ] && [ "$(grep -cE 'MH-LoRa:? ' "$LOGFILE")" -eq 0 ]; then
    MH_SYNTH=1
    new_tmp
    awk '
    { print }
    # No MH-LoRa for ACK frames (printBuffer_ack: no ">" path) or >HN@
    # reports (early return in src/lora_functions.cpp), so no synthetic one.
    /\[LOG\] [0-9][0-9][0-9] / && / RSSI:-?[0-9]+ SNR:-?[0-9]+ DUP:/ && />/ && !/>HN@/ {
        l = $0
        sub(/ \[LOG\] /, " MH-LoRa ", l)
        sub(/ RSSI:-?[0-9]+ SNR:-?[0-9]+ DUP:.*$/, "", l)
        print l
    }' "$LOGFILE" > "$NEW_TMP"
    LOGFILE="$NEW_TMP"
    echo "MH_LORA_SOURCE: [LOG] RX lines (no MH-LoRa lines in log, --info off)"
fi

# ─── 2. ACTIVE NODES ───
section "NODES"

# Single-pass awk: correlate "Received packet:" RSSI/SNR with subsequent MH-LoRa lines.
# Extract callsign from route field (first callsign before comma or ">").
awk -v sl="$SL_RX_N" '
# SETLOG RX tail is the primary RSSI/SNR source (the only one on nRF52). When
# the log has any, "Received packet:" is ignored so no frame is counted twice.
sl > 0 && /\[LOG\] [0-9][0-9][0-9] / && / RSSI:-?[0-9]+ SNR:-?[0-9]+ DUP:/ {
    for (i=NF; i>=1; i--) {
        if ($i ~ /^SNR:-?[0-9]+$/) { last_snr = substr($i, 5) + 0 }
        if ($i ~ /^RSSI:-?[0-9]+$/) { last_rssi = substr($i, 6) + 0; break }
    }
    has_rssi = 1
    next
}
# Note: with LogCallsign filtering active the firmware may print the tail for
# only some frames, but tail presence (sl > 0) suppresses "Received packet:"
# for ALL lines, including frames that have no tail.
sl > 0 && /Received packet:/ { next }
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

/MH-LoRa:? / {
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
echo "UNIQUE_NODES: $(grep -E 'MH-LoRa:? ' "$LOGFILE" | awk '{for(i=1;i<=NF;i++){if($i~/>/){{split($i,r,/[,>]/);if(r[1]!=""&&r[1]!~/^[0-9]/)print r[1];break}}}}' | sort -u | wc -l | tr -d ' ')"
echo "TOTAL_MH_PACKETS: $(grep -cE 'MH-LoRa:? ' "$LOGFILE")"

# ─── 3. MESSAGE TYPES ───
section "MESSAGE_TYPES"

# Packet type char is the token two after the tag (`MH-LoRa 085 : x...`;
# older firmware prints the tag with a colon): printBuffer_aprs' %c payload
# type. `@` = heartbeat (HG@R / H@R / HN@ neighbour reports), `!` = position,
# `:` = text, anything else = other. Never test the whole line: the timestamp
# contains ":", `*` is the broadcast destination fused into the route token,
# and "H@R" / "*!" can appear inside user text.
grep -E 'MH-LoRa:? ' "$LOGFILE" | awk '
{
    ptype = ""
    for (i=1; i<=NF; i++) {
        if ($i ~ /^MH-LoRa:?$/) { ptype = $(i+2); break }
    }
    if (ptype == "@") heartbeat++
    else if (ptype == "!") position++
    else if (ptype == ":") text++
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
[ "$MH_SYNTH" -eq 1 ] && echo "n/a (no MH-LoRa lines, --info off)"
# RX-LoRa-All: has no emitter in current firmware; derive from accepted text
# frames (type char ":") instead, heartbeats excluded.
grep -E 'RX-LoRa2:? +[0-9]{3} :' "$LOGFILE" | grep -vE 'H@R|HG@R' | head -20 || true

# ─── 4. HOP DISTRIBUTION ───
section "HOP_DISTRIBUTION"

echo "--- MH-LoRa (Empfangen) ---"
grep -E 'MH-LoRa:? ' "$LOGFILE" | grep -oE 'H[0-9]{2} S[0-9]' | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

echo "--- RX-LoRa2 (Akzeptiert) ---"
[ "$MH_SYNTH" -eq 1 ] && echo "n/a (no MH-LoRa lines, --info off)"
grep -E 'RX-LoRa2:? ' "$LOGFILE" | grep -oE 'H[0-9]{2} S[0-9]' | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

echo "--- TX-LoRa (Gesendet) ---"
[ "$MH_SYNTH" -eq 1 ] && echo "n/a (no MH-LoRa lines, --info off)"
grep -E 'TX-LoRa:? +[0-9]{3} ' "$LOGFILE" | grep -oE 'H[0-9]{2} S[0-9]' | grep -oE 'H[0-9]{2}' | sort | uniq -c | sort -rn || true

# ─── 5. LOOPS ───
section "LOOPS"

echo "RELAY_LOOP_BLOCKED: $(grep -c 'RELAY_LOOP_BLOCKED' "$LOGFILE" 2>/dev/null; true)"
grep "RELAY_LOOP_BLOCKED" "$LOGFILE" | head -10 || true

# ─── 6. CHANNEL UTILIZATION ───
section "CHANNEL_UTIL"

# F-12 billing check. ESP32 builds before f2d6532f billed every RX frame as
# 255 B of airtime, so each rx= window is a multiple of one fixed frame time
# (GCD of the nonzero rx= values), and every CRC dump is padded to size=255.
# Either signal marks the util figures as overstated. CRC condition = same as
# CRC_FORENSICS (>= 3 CRC_ERROR lines, all size=255).
BILLING_OVERSTATED=0
BILLING_LINE=$(awk '
function gcd(a, b,    t) { while (b > 0) { t = a % b; a = b; b = t } return a }
/CHANNEL_UTIL/ {
    for (i = 1; i <= NF; i++) if ($i ~ /^rx=[0-9]+/) {
        v = substr($i, 4) + 0
        if (v > 0) { nz++; g = (nz == 1) ? v : gcd(g, v) }
    }
}
/CRC_ERROR/ {
    crc_n++
    if (match($0, /size=[0-9]+/) && substr($0, RSTART+5, RLENGTH-5) + 0 == 255) crc_255++
}
END { printf "%d %d %d %d\n", g + 0, nz + 0, crc_n + 0, crc_255 + 0 }' "$LOGFILE")
read -r BILL_GCD BILL_NZ BILL_CRC_N BILL_CRC_255 <<< "$BILLING_LINE"
BILL_SIG=""
BILL_UNIT=""
if [ "$BILL_NZ" -ge 20 ] && [ "$BILL_GCD" -ge 500 ]; then
    BILL_SIG="rx grid: GCD of $BILL_NZ nonzero rx= values is $BILL_GCD ms"
    BILL_UNIT=" (rx unit ${BILL_GCD} ms)"
fi
if [ "$BILL_CRC_N" -ge 3 ] && [ "$BILL_CRC_N" -eq "$BILL_CRC_255" ]; then
    BILL_SIG="${BILL_SIG:+$BILL_SIG; }every CRC_ERROR size=255 ($BILL_CRC_N lines)"
fi
if [ -n "$BILL_SIG" ]; then
    BILLING_OVERSTATED=1
    echo "WARNING: ESP32 build < f2d6532f: rx billed as 255 B per frame${BILL_UNIT}, util and CRC payloads not comparable"
    echo "  signal: $BILL_SIG"
    echo "  rx= and util= below are overstated (about 1.8-4.1x)"
else
    echo "BILLING_CHECK: ok"
fi

new_tmp
CHANNEL_UTIL_TMP="$NEW_TMP"
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
}' > "$CHANNEL_UTIL_TMP"

if [ -s "$CHANNEL_UTIL_TMP" ]; then
    awk -v overstated="$BILLING_OVERSTATED" '
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
        if (overstated) print "NOTE: util figures below are overstated (airtime billing, see WARNING above)"
        printf "SAMPLES: %d\n", count
        printf "AVG: %.1f%%\n", avg
        printf "MIN: %.1f%%\n", min
        printf "MAX: %.1f%%\n", max
        printf "FIRST_HALF_AVG: %.1f%%\n", avg1
        printf "SECOND_HALF_AVG: %.1f%%\n", avg2
        printf "TREND: %s\n", (avg2 > avg1 * 1.1) ? "STEIGEND" : (avg1 > avg2 * 1.1) ? "FALLEND" : "STABIL"
        printf "SAMPLES_GE_70: %d\n", high70
        printf "SAMPLES_GE_90: %d\n", high90
    }' "$CHANNEL_UTIL_TMP"

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
    }' "$CHANNEL_UTIL_TMP" | sort
else
    echo "NO_DATA"
fi

# ─── 7. ACK ANALYSIS ───
section "ACK_ANALYSIS"

# Live ACK markers only (see src/lora_functions.cpp). The pre-4.30 markers
# (ACK_FAST_*, ACK_RX_CANCEL, *_DEDUP, *_DROPPED, ack_qlen, ...) have no
# emitter any more and were removed.
echo "ACK_RECEIVED: $(grep -c 'ACK_RECEIVED retid=' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_SKIP_READY: $(grep -c 'ACK_SKIP_READY slot=' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_REJECT: $(grep -c 'ACK_REJECT b5=' "$LOGFILE" 2>/dev/null; true)"
echo "ACK_APPENDIX: $(grep -c 'ACK_APPENDIX hash=' "$LOGFILE" 2>/dev/null; true)"
# ACK frames go out as "TX-Lora " (lowercase), not "TX-LoRa " -- counted apart.
echo "TX_ACK_FRAMES (TX-Lora): $(grep -cE 'TX-Lora +[0-9]{3} ' "$LOGFILE" 2>/dev/null; true)"

# ─── 8. CRC ERRORS ───
# Matches three log formats:
#   ESP32:      [MC-DBG] CRC_ERROR rssi=-125 snr=-20 freq_err=134.7 size=255
#   nRF52 new:  [MC-DBG] RX_ERROR rssi=-125 snr=-20 ts=12345
#   nRF52 old:  OnRxError   (bare, no details)
section "CRC_ERRORS"

CRC_ESP_COUNT=$(grep -c 'CRC_ERROR' "$LOGFILE" 2>/dev/null || true)
CRC_NRF_NEW_COUNT=$(grep -c 'RX_ERROR rssi' "$LOGFILE" 2>/dev/null || true)
CRC_NRF_OLD_COUNT=$(grep -c 'OnRxError$' "$LOGFILE" 2>/dev/null || true)
# OnRxError always comes paired with a detail line:
#   ESP32:      OnRxError -> CRC_ERROR (with rssi/snr/freq_err)
#   nRF52 new:  OnRxError -> RX_ERROR  (with rssi/snr)
#   nRF52 old:  OnRxError alone        (no detail line)
# Only count OnRxError if there are NO CRC_ERROR and NO RX_ERROR lines (old nRF52 FW)
# SETLOG `[LOG] ERR rssi= snr= len= ferr= t=` (SL-04) is the RX-loss source
# when none of the [MC-DBG] CRC_ERROR / RX_ERROR detail lines exist (audit
# F-14): it works on ESP32 and nRF52 and also covers non-CRC RX errors.
SL_ERR_N=$(grep -cE '\[LOG\] ERR ' "$LOGFILE" 2>/dev/null; true)
if [ "$CRC_ESP_COUNT" -gt 0 ] || [ "$CRC_NRF_NEW_COUNT" -gt 0 ]; then
    CRC_SRC="mcdbg"
    CRC_DETAIL_RE='CRC_ERROR|RX_ERROR rssi'
    CRC_NRF_COUNT=$CRC_NRF_NEW_COUNT
elif [ "$SL_ERR_N" -gt 0 ]; then
    CRC_SRC="setlog"
    CRC_DETAIL_RE='[[]LOG[]] ERR '
    CRC_NRF_COUNT=0
else
    CRC_SRC="onrxerror"
    CRC_DETAIL_RE='OnRxError$'
    CRC_NRF_COUNT=$CRC_NRF_OLD_COUNT
fi
CRC_TOTAL=$((CRC_ESP_COUNT + CRC_NRF_COUNT))
if [ "$CRC_SRC" = "setlog" ]; then CRC_TOTAL=$SL_ERR_N; fi
echo "CRC_ERROR_COUNT: $CRC_TOTAL"
# M-08: non-CRC radio errors (esp32_main.cpp: RX_OTHER_ERROR code= rssi= snr= size= ts=)
RXOE_SEC_N=$(grep -c 'RX_OTHER_ERROR code=' "$LOGFILE" 2>/dev/null || true)
echo "RX_OTHER_ERROR_COUNT: ${RXOE_SEC_N:-0}"
if [ "${RXOE_SEC_N:-0}" -gt 0 ]; then
    grep 'RX_OTHER_ERROR code=' "$LOGFILE" | awk '
    { if (match($0, /code=-?[0-9]+/)) c[substr($0, RSTART+5, RLENGTH-5) + 0]++ }
    END { for (k in c) printf "%d %d\n", k, c[k] }' | sort -n | awk '{ printf "RX_OTHER_ERROR code=%d: %d\n", $1, $2 }'
fi
if [ "$CRC_ESP_COUNT" -gt 0 ]; then echo "CRC_ESP32 (CRC_ERROR): $CRC_ESP_COUNT"; fi
if [ "$CRC_NRF_COUNT" -gt 0 ] && [ "$CRC_SRC" = "mcdbg" ]; then echo "CRC_NRF52 (RX_ERROR): $CRC_NRF_COUNT"; fi
if [ "$CRC_SRC" = "setlog" ]; then
    echo "CRC_SOURCE: setlog ERR ($SL_ERR_N lines; any RX error, not CRC only)"
    grep -E '\[LOG\] ERR ' "$LOGFILE" | awk '
    {
        r = ""; sn = ""; ln = ""
        for (i=1; i<=NF; i++) {
            if ($i ~ /^rssi=-?[0-9]+$/) r = substr($i, 6) + 0
            if ($i ~ /^snr=-?[0-9]+$/) sn = substr($i, 5) + 0
            if ($i ~ /^len=[0-9]+$/) ln = substr($i, 5) + 0
        }
        n++
        if (r != "") { if (rn == 0 || r < rmin) rmin = r; if (rn == 0 || r > rmax) rmax = r; rn++ }
        if (sn != "") { if (sc == 0 || sn < smin) smin = sn; if (sc == 0 || sn > smax) smax = sn; sc++ }
        if (ln == 0 && ln != "") len0++
    }
    END {
        if (rn > 0) printf "ERR_RSSI_RANGE: %d..%d\n", rmin, rmax; else print "ERR_RSSI_RANGE: n/a"
        if (sc > 0) printf "ERR_SNR_RANGE: %d..%d\n", smin, smax; else print "ERR_SNR_RANGE: n/a"
        printf "ERR_LEN0: %d\n", len0 + 0
    }'
elif [ "$CRC_SRC" = "onrxerror" ] && [ "$CRC_NRF_OLD_COUNT" -gt 0 ]; then
    # Bare OnRxError has no cause. "Received packet:" only exists on ESP32,
    # where bare OnRxError may be a non-CRC error -- never label that nRF52.
    # ESP32 [HEAP] forms start the tag after whitespace/line start; nRF52 prints
    # `HH:MM:SS;[HEAP];...` (tag after ";"), so that form is no ESP32 evidence.
    if [ "${SL_HAVE_RCV:-0}" -gt 0 ] || grep -qE '(^|[[:space:]])\[HEAP\]' "$LOGFILE"; then
        echo "RXERROR_BARE (ESP32 OnRxError, cause not logged, not necessarily CRC): $CRC_NRF_OLD_COUNT"
    else
        echo "CRC_NRF52 (OnRxError, no details): $CRC_NRF_OLD_COUNT"
    fi
fi

# Detail lines: structured [MC-DBG] lines, else SETLOG ERR, else bare OnRxError
grep -E "$CRC_DETAIL_RE" "$LOGFILE" | awk '{
    rssi="n/a"; snr="n/a"; ferr="n/a"; sz="n/a"
    for (i=1; i<=NF; i++) {
        if ($i ~ /^rssi=/) rssi = $i
        if ($i ~ /^snr=/) snr = $i
        if ($i ~ /^(freq_err|ferr)=/) ferr = $i
        if ($i ~ /^(size|len)=/) sz = $i
    }
    print $2, rssi, snr, ferr, sz
}' | head -50 || true

# Classify by freq error (only possible for ESP32 CRC_ERROR lines with freq_err)
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
NRF_UNCLASS=$CRC_NRF_COUNT
# Also count OnRxError-only entries from old nRF52 FW
if [ "$CRC_ESP_COUNT" -eq 0 ] && [ "$CRC_NRF_NEW_COUNT" -eq 0 ] && [ "$CRC_NRF_OLD_COUNT" -gt 0 ]; then
    NRF_UNCLASS=$CRC_NRF_OLD_COUNT
fi
if [ "$NRF_UNCLASS" -gt 0 ]; then
    echo "UNCLASSIFIED (nRF52, no freq_err): $NRF_UNCLASS"
fi

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

RETX_N=$(grep -c 'RETRANSMIT retry=' "$LOGFILE" 2>/dev/null; true)
GIVEUP_N=$(grep -c 'RETRANSMIT_GIVEUP retries=' "$LOGFILE" 2>/dev/null; true)
GIVEUP_DM_N=$(grep -c 'RETRANSMIT_GIVEUP_DM' "$LOGFILE" 2>/dev/null; true)
ACKRX_N=$(grep -c 'ACK_RECEIVED retid=' "$LOGFILE" 2>/dev/null; true)
echo "RETRANSMIT: ${RETX_N:-0}"
echo "RETRANSMIT_GIVEUP: ${GIVEUP_N:-0}"
echo "RETRANSMIT_GIVEUP_DM: ${GIVEUP_DM_N:-0}"
if [ "${RETX_N:-0}" -eq 0 ] && [ "${GIVEUP_N:-0}" -eq 0 ] && [ "${GIVEUP_DM_N:-0}" -eq 0 ] && [ "${ACKRX_N:-0}" -eq 0 ]; then
    echo "MISSING_ACKS_VERDICT: n/a (no RETRANSMIT/ACK_RECEIVED events in log)"
fi

# ─── 11. DEDUP ───
section "DEDUP"

echo "DEDUP_EXPLICIT: $(grep -c 'RX_DEDUP_DUP msg_id=' "$LOGFILE" 2>/dev/null; true)"
echo "RX_DEDUP_NEW: $(grep -c 'RX_DEDUP_NEW' "$LOGFILE" 2>/dev/null; true)"

MH_COUNT=$(grep -cE "MH-LoRa:? " "$LOGFILE" 2>/dev/null; true)
RX_COUNT=$(grep -cE "RX-LoRa2:? " "$LOGFILE" 2>/dev/null; true)
echo "MH_LORA_HEARD: $MH_COUNT"
if [ "$MH_SYNTH" -eq 1 ]; then
    echo "RX_LORA_ACCEPTED: n/a (no RX-LoRa2 lines, --info off)"
else
    echo "RX_LORA_ACCEPTED: $RX_COUNT"
fi
if [ "$MH_SYNTH" -eq 1 ]; then
    echo "IMPLICIT_DEDUP: n/a (no RX-LoRa2 lines, --info off)"
else
    echo "IMPLICIT_DEDUP: $((MH_COUNT - RX_COUNT))"
fi

# Top repeated msg_ids
echo ""
echo "--- TOP_REPEATED_MSGIDS ---"
grep -E 'MH-LoRa:? ' "$LOGFILE" | grep -oE 'x[A-F0-9]{8}' | sort | uniq -c | sort -rn | head -10 || true

# Max duplication factor
echo ""
echo "--- DUPLICATION_DISTRIBUTION ---"
grep -E 'MH-LoRa:? ' "$LOGFILE" | grep -oE 'x[A-F0-9]{8}' | sort | uniq -c | awk '{print $1}' | sort -n | uniq -c | sort -rn || true

# ─── 11b. SETLOG (firmware `--setlog on` lines, src/setlog_lines.h) ───
# `HH:MM:SS [LOG] <body>`: RX tail (SL-01), RLY (SL-02), TX (SL-03), ERR
# (SL-04), STAT (SL-05, every 300 s, 5-min window), GWI/GWU (SL-06). The
# marker is matched anywhere in the line (logger prefix, junk before the
# time). Sections without data print n/a, never a healthy verdict.
section "SETLOG"

if [ "$SL_N" -eq 0 ]; then
    echo "n/a (no [LOG] setlog lines; enable with --setlog on)"
else
    new_tmp
    SL_TXW="$NEW_TMP"
    # DEDUP_BALANCE: the three sources count different things, so they are
    # compared pairwise with the matching correction (never all three raw):
    #  - [MC-DBG] RX_DEDUP_NEW/DUP come from is_new_packet(), which on an
    #    ESP32 gateway also fires for UDP frames (one GWI line each); NEW is
    #    compared after subtracting the GWI count.
    #  - STAT newid/dup come from setlogCountDedup(): it skips HN reports,
    #    own and gateway-injected echoes (msg_id seen in `[LOG] TX ... src=o|g`)
    #    and gains the S2 server-inject increments (= GWI). STAT counters are
    #    per 300 s window and the first window predates the capture, so the
    #    tail is counted strictly after the first STAT line up to the last
    #    one and STAT is summed from the 2nd line on.
    # Tolerance = max(5, 1% of the larger of the two compared values).
    awk -v txw="$SL_TXW" '
    function tok(k,   i, n, p) {
        # value of "k=..." among the fields, "" when absent
        for (i = NF; i >= 1; i--) {
            n = length(k) + 1
            if (substr($i, 1, n) == k "=") return substr($i, n + 1)
        }
        return ""
    }
    /RX_DEDUP_NEW msg_id=/ { dbg_new++ }
    /RX_DEDUP_DUP msg_id=/ { dbg_dup++ }

    function msgid(   i) {
        for (i = 1; i <= NF; i++)
            if (length($i) == 9 && $i ~ /^x[0-9A-F]+$/) return $i
        return ""
    }
    /\[LOG\] [0-9][0-9][0-9] / && / RSSI:-?[0-9]+ SNR:-?[0-9]+ DUP:[nd] OWN:/ {
        rx_n++
        # windowed tail (STAT comparison): after the first STAT line, no HN
        # reports, no own/gateway-injected echoes (ids from `TX ... src=o|g`)
        if (stat_n >= 1 && $0 !~ />HN@/ && !(msgid() in own_ids)) {
            if ($0 ~ / DUP:n /) w_new++
            else w_dup++
        }
        for (i = NF; i >= 1; i--) {
            if ($i ~ /^SNR:-?[0-9]+$/) sn = substr($i, 5) + 0
            if ($i == "DUP:d") tail_dup++
            if ($i == "DUP:n") tail_new++
            if ($i == "OWN:e") own_echo++
            if ($i ~ /^RSSI:-?[0-9]+$/) { r = substr($i, 6) + 0; break }
        }
        if (rx_n == 1 || r < rssi_min) rssi_min = r
        if (rx_n == 1 || r > rssi_max) rssi_max = r
        rssi_sum += r
        if (rx_n == 1 || sn < snr_min) snr_min = sn
        if (rx_n == 1 || sn > snr_max) snr_max = sn
        snr_sum += sn
        next
    }

    /\[LOG\] RLY x/ {
        rly_n++
        q = tok("q")
        if (q == "") q = "?"
        rly[q]++
        next
    }

    /\[LOG\] TX x/ {
        tx_n++
        src = tok("src"); w = tok("wait"); c = tok("cad")
        if (src == "o" || src == "g") own_ids[msgid()] = 1
        if (src !~ /^[org]$/) src = "?"
        txc[src]++
        if (c ~ /^[0-9]+$/ && c + 0 > 0) cad_gt0++
        if (w ~ /^[0-9]+$/) print src, w > txw
        next
    }

    /\[LOG\] GWI x/ {
        gwi_n++
        if (stat_n >= 1) w_gwi++
        f = tok("from"); if (f == "") f = "?"
        gwi_from[f]++
        next
    }
    /\[LOG\] GWU x/ { gwu_n++; next }

    /\[LOG\] STAT / {
        stat_n++
        u = tok("util")
        if (u ~ /^[0-9]+$/) {
            u += 0; u_n++; u_sum += u; u_last = u
            if (u_n == 1 || u < u_min) u_min = u
            if (u_n == 1 || u > u_max) u_max = u
        }
        v = tok("newid"); if (v ~ /^[0-9]+$/) { s_newid += v; if (stat_n > 1) sw_newid += v }
        v = tok("dup");   if (v ~ /^[0-9]+$/) { s_dup += v; if (stat_n > 1) sw_dup += v }
        # window snapshot: the tail counted so far is what the STAT lines
        # up to this one can account for (the last snapshot wins)
        sn_new = w_new; sn_dup = w_dup; sn_gwi = w_gwi
        v = tok("err");   if (v ~ /^[0-9]+$/) s_err += v
        v = tok("txn");   if (v ~ /^[0-9]+$/) s_txn += v
        v = tok("txfail"); if (v ~ /^[0-9]+$/) s_txfail += v
        v = tok("ringmax")
        if (v ~ /^[0-9]+\/[0-9]+$/) {
            split(v, rm, "/")
            if (rm[1] + 0 > ringmax) ringmax = rm[1] + 0
            ringsize = rm[2] + 0
        }
        v = tok("drop")
        if (v ~ /^[0-9]+\/[0-9]+\/[0-9]+\/[0-9]+\/[0-9]+$/) {
            split(v, dp, "/")
            for (k = 1; k <= 5; k++) drop[k] += dp[k]
            drop_n++
        }
        v = tok("heap")
        if (v ~ /^[0-9]+$/) {
            v += 0; h_n++; h_last = v
            if (h_n == 1) h_first = v
            if (h_n == 1 || v < h_min) h_min = v
        }
        v = tok("trk"); if (v != "") trk_last = v
        v = tok("fw")
        if (v != "" && !(v in fw_seen)) { fw_seen[v] = 1; fw_list = fw_list (fw_list == "" ? "" : " ") v }
        v = tok("up")
        if (v ~ /^[0-9]+$/) {
            v += 0
            if (up_seen && v < up_prev) reboots++
            up_prev = v; up_seen = 1
        }
        next
    }

    function tol(a, b,   m) {
        m = (a > b ? a : b) * 0.01
        return (m < 5 ? 5 : m)
    }
    # label, left value + description, right value + description
    function pair(label, a, b, ok, desc,   d, t) {
        if (!ok) { printf "%s: n/a\n%s_BALANCE: n/a (needs both sources)\n", label, label; return }
        printf "%s: %s\n", label, desc
        d = a - b; if (d < 0) d = -d
        t = tol(a, b)
        if (d > t) printf "%s_BALANCE: MISMATCH (diff %d > tolerance %.0f)\n", label, d, t
        else printf "%s_BALANCE: OK (diff %d <= tolerance %.0f)\n", label, d, t
    }

    END {
        print "--- RX_TAIL ---"
        if (rx_n > 0) {
            printf "RX_TAIL_LINES: %d\n", rx_n
            printf "RX_RSSI: %d..%d avg %.1f\n", rssi_min, rssi_max, rssi_sum / rx_n
            printf "RX_SNR: %d..%d avg %.1f\n", snr_min, snr_max, snr_sum / rx_n
            printf "RX_NEW (DUP:n): %d\n", tail_new
            printf "RX_DUP (DUP:d): %d\n", tail_dup
            printf "DUP_RATE: %.1f%%\n", tail_dup * 100.0 / rx_n
            printf "OWN_ECHO (OWN:e): %d\n", own_echo
        } else print "n/a (no RX lines with RSSI/SNR/DUP/OWN tail)"

        print ""
        print "--- RLY_REASONS ---"
        if (rly_n > 0) {
            printf "RLY_TOTAL: %d\n", rly_n
            split("tx unconf self aprs nomesh gwfilter gwcap ping hop0 loop full", ord, " ")
            for (k = 1; k <= 11; k++) printf "  q=%s: %d\n", ord[k], rly[ord[k]] + 0
            for (q in rly) {
                known = 0
                for (k = 1; k <= 11; k++) if (ord[k] == q) known = 1
                if (!known) printf "  q=%s: %d\n", q, rly[q]
            }
            if (rly["full"] > 0) printf "FINDING: RLY q=full %d (relay ring full, frames not relayed)\n", rly["full"]
        } else print "n/a (no RLY lines)"

        print ""
        print "--- TX_SPLIT ---"
        if (tx_n > 0) {
            printf "TX_TOTAL: %d\n", tx_n
            printf "TX_OWN (src=o): %d\n", txc["o"] + 0
            printf "TX_RELAY (src=r): %d\n", txc["r"] + 0
            printf "TX_GW (src=g): %d\n", txc["g"] + 0
            if (txc["?"] > 0) printf "TX_OTHER (src unknown): %d\n", txc["?"]
            printf "TX_CAD_GT0: %d\n", cad_gt0 + 0
        } else print "n/a (no TX lines)"

        print ""
        print "--- STAT ---"
        if (stat_n > 0) {
            printf "STAT_LINES: %d\n", stat_n
            if (u_n > 0) printf "UTIL: min %d avg %.1f max %d last %d (5-min windows, %%)\n", u_min, u_sum / u_n, u_max, u_last
            else print "UTIL: n/a"
            printf "SUM newid=%d dup=%d err=%d txn=%d txfail=%d\n", s_newid, s_dup, s_err, s_txn, s_txfail
            if (ringsize > 0) printf "RINGMAX: %d/%d\n", ringmax, ringsize
            else print "RINGMAX: n/a"
            if (drop_n > 0) {
                printf "DROP_TOTAL: p1=%d p2=%d p3=%d p4=%d p5=%d\n", drop[1], drop[2], drop[3], drop[4], drop[5]
                for (k = 1; k <= 5; k++)
                    if (drop[k] > 0) printf "FINDING: STAT drop p%d = %d (TX ring drops)\n", k, drop[k]
            } else print "DROP_TOTAL: n/a"
            if (h_n > 0) printf "HEAP: first %d min %d last %d\n", h_first, h_min, h_last
            else print "HEAP: n/a"
            printf "TRK_LAST: %s\n", (trk_last == "" ? "n/a" : trk_last)
            printf "FW: %s\n", (fw_list == "" ? "n/a" : fw_list)
            if (up_seen) printf "REBOOTS (up= decreasing): %d\n", reboots + 0
            else print "REBOOTS: n/a"
            if (s_txfail > 0) printf "FINDING: STAT txfail = %d (TX watchdog aborts)\n", s_txfail
        } else print "n/a (no STAT lines)"

        print ""
        print "--- GATEWAY ---"
        if (gwi_n + gwu_n > 0) {
            printf "GWI: %d\n", gwi_n + 0
            printf "GWU: %d\n", gwu_n + 0
            nf = 0
            for (f in gwi_from) nf++
            printf "GWI_SENDERS: %d (top 10 by count)\n", nf
            fflush()
            for (f in gwi_from) printf "  GWI from=%s: %d\n", f, gwi_from[f] | "sort -t: -k2 -rn | head -10"
            close("sort -t: -k2 -rn | head -10")
        } else print "n/a (no GWI/GWU lines)"

        print ""
        print "--- DEDUP_BALANCE ---"
        hd = (dbg_new + dbg_dup > 0); ht = (rx_n > 0); hs = (stat_n >= 2)
        # [MC-DBG] vs tail, whole file; NEW minus the UDP/GWI frames
        pair("NEW_DBG", dbg_new - gwi_n, tail_new, hd && ht, \
            sprintf("RX_DEDUP_NEW=%d - GWI=%d = %d, DUP:n=%d", dbg_new, gwi_n, dbg_new - gwi_n, tail_new))
        pair("DUP_DBG", dbg_dup, tail_dup, hd && ht, \
            sprintf("RX_DEDUP_DUP=%d, DUP:d=%d", dbg_dup, tail_dup))
        # STAT (2nd line on) vs windowed tail without HN / own / gw echoes
        pair("NEW_STAT", sw_newid, sn_new + sn_gwi, hs && ht, \
            sprintf("STAT_newid=%d, DUP:n=%d + GWI=%d = %d (STAT window, no HN/own/gw echoes)", sw_newid, sn_new, sn_gwi, sn_new + sn_gwi))
        pair("DUP_STAT", sw_dup, sn_dup, hs && ht, \
            sprintf("STAT_dup=%d, DUP:d=%d (STAT window, no HN/own/gw echoes)", sw_dup, sn_dup))
    }' "$LOGFILE"

    echo ""
    echo "--- TX_WAIT (ms, per class) ---"
    for _cls in o r g; do
        case "$_cls" in o) _nm="own (src=o)";; r) _nm="relay (src=r)";; g) _nm="gw (src=g)";; esac
        awk -v c="$_cls" '$1 == c { print $2 }' "$SL_TXW" | sort -n | awk -v nm="$_nm" '
            { a[NR] = $1; s += $1 }
            END {
                if (NR == 0) { printf "%s: n/a\n", nm; exit }
                med = (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2
                printf "%s: count %d avg %.0f median %.0f max %d\n", nm, NR, s / NR, med, a[NR]
            }'
    done
fi

# ─── 12. STATE MACHINE ───
section "STATE_MACHINE"

echo "MC_SM_TOTAL: $(grep -c 'MC-SM' "$LOGFILE" 2>/dev/null; true)"
echo "MC_SM_ERRORS: $(grep 'MC-SM' "$LOGFILE" | grep -v 'rc=0' | grep -vc 'TX_PREPARE -> IDLE rc=-1' 2>/dev/null; true)"
echo "MC_SM_CSMA_BACKOFF: $(grep -c 'TX_PREPARE -> IDLE rc=-1' "$LOGFILE" 2>/dev/null; true)"
grep "MC-SM" "$LOGFILE" | grep -v "rc=0" | grep -v 'TX_PREPARE -> IDLE rc=-1' | head -10 || true

# ─── 13. ADDITIONAL CHECKS ───
section "ADDITIONAL"

echo "--- WIFI_ISSUES ---"
# Current firmware: one "[WIFI];event;disconnected" per drop plus
# "[WIFI];stall" (the same events as WIFI_DISCONNECT/WIFI_STALL below).
# [WIFI];assoc;disconnected, [SAFEBOOT];wifi;event;disconnected and the
# [WIFI-DBG] lines describe the same drop and would count it again.
# The broad legacy pattern is used only when the log has no ";event;" lines;
# packet lines (payload text) and the 2323/web "Client disconnected" never count.
# meshlogger's own "[LOGGER] end ... reconnects=N" line is not a node event either.
# `[LOG] NNN ` RX lines (--setlog) carry the same payload text as MH-LoRa.
PKT_RE="$PAYLOAD_LINE_RE"'|\[LOGGER\]'
if grep -qE '\[WIFI\];event;' "$LOGFILE"; then
    WIFI_ISSUE_RE='\[WIFI\];event;disconnected|\[WIFI\];stall'
else
    WIFI_ISSUE_RE='disconnect|reconnect|WIFI.*fail'
fi
{ grep -iE "$WIFI_ISSUE_RE" "$LOGFILE" | grep -vE "$PKT_RE" | grep -v 'Client disconnected'; } | head -10 || true
echo "WIFI_ISSUE_COUNT: $({ grep -iE "$WIFI_ISSUE_RE" "$LOGFILE" | grep -vE "$PKT_RE" | grep -vc 'Client disconnected'; } 2>/dev/null; true)"

echo ""
echo "--- CRASHES ---"
# One crash = one "[BOOT] RESET_REASON=<n> <NAME>" line with a crash reason
# (src/esp32/esp32_main.cpp prints it once per boot). "Rebooting..." follows
# every OTA, "rst:0x" is every ROM boot banner, and one panic prints several
# lines (Guru Meditation, Backtrace, abort), so none of those are counted.
# Only a log without any RESET_REASON line (older firmware, other captures)
# falls back to counting the panic/abort/watchdog banners.
CRASH_RESET_RE='RESET_REASON=[0-9]+ (PANIC|INT_WDT|TASK_WDT|WDT|BROWNOUT)'
CRASH_BANNER_RE='Guru Meditation Error|abort\(\) was called|Task watchdog got triggered'
if grep -qE 'RESET_REASON=' "$LOGFILE"; then
    CRASH_COUNT_RE=$CRASH_RESET_RE
else
    CRASH_COUNT_RE=$CRASH_BANNER_RE
fi
# Sample listing: the counted lines plus panic/backtrace detail.
{ grep -E "$CRASH_RESET_RE|Guru Meditation|panic|[Bb]acktrace|abort\(\)|Task watchdog got triggered" "$LOGFILE" | grep -vE "$PAYLOAD_LINE_RE"; } | head -10 || true
echo "CRASH_COUNT: $({ grep -E "$CRASH_COUNT_RE" "$LOGFILE" | grep -vcE "$PAYLOAD_LINE_RE"; } 2>/dev/null; true)"
echo "RESET_REASON histogram:"
{ grep -oE 'RESET_REASON=[0-9]+ [A-Z_]+' "$LOGFILE" | sort | uniq -c; } || true
echo "TX_WATCHDOG_FIRED: $(grep -c '\[MC-WDT\] TX_WATCHDOG fired' "$LOGFILE" 2>/dev/null; true)"

echo ""
echo "--- HEAP_TREND ---"
# "[HEAP]" followed by ";" (--debug csv, nRF52) or " " (default ESP32 form,
# printfdeb strips ";"): "[HEAP]Cnew;" / "[HEAP]Cdis;" are web-client samples.
{ grep -E '\[HEAP\][; ]' "$LOGFILE" || true; } | head -5
echo "..."
{ grep -E '\[HEAP\][; ]' "$LOGFILE" || true; } | tail -5
HEAP_SAMPLES=$(grep -cE '\[HEAP\][; ]' "$LOGFILE" 2>/dev/null; true)
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
BUFDROP_RE='RING_DROP_|RING_REJECT|RX_BUF_OVERWRITE|CAPTURE_DROPPED'
echo "BUFFER_DROPS: $(grep -cE "$BUFDROP_RE" "$LOGFILE" 2>/dev/null; true)"
grep -E "$BUFDROP_RE" "$LOGFILE" | head -10 || true

echo ""
echo "--- CAD_STATS ---"
# CAD_BUSY_1 = initial busy (double-check follows); "CAD_BUSY attempt=" =
# confirmed busy. CAD false-positive rate = CAD_FALSE_POSITIVE / CAD_BUSY_1.
echo "CAD_BUSY_1: $(grep -c 'CAD_BUSY_1' "$LOGFILE" 2>/dev/null; true)"
echo "CAD_BUSY_CONFIRMED: $(grep -c 'CAD_BUSY attempt=' "$LOGFILE" 2>/dev/null; true)"
echo "CAD_FALSE_POSITIVE: $(grep -c 'CAD_FALSE_POSITIVE' "$LOGFILE" 2>/dev/null; true)"
echo "CAD_ABORT_BY_RX: $(grep -c 'CAD_ABORT_BY_RX' "$LOGFILE" 2>/dev/null; true)"
echo "CAD_SAFETY_TIMEOUT: $(grep -c 'CAD_SAFETY_TIMEOUT' "$LOGFILE" 2>/dev/null; true)"
echo "RX_TIMEOUT_DEFERRED: $(grep -c 'RX_TIMEOUT_DEFERRED' "$LOGFILE" 2>/dev/null; true)"

# ─── 14. CRC DETAIL ───
section "CRC_DETAIL"

echo "--- CRC_10MIN_BUCKETS ---"
# Use CRC_ERROR + RX_ERROR (structured detail lines), fall back to OnRxError (old nRF52)
grep -E "$CRC_DETAIL_RE" "$LOGFILE" | awk '{
    split($2, t, ":")
    bucket = t[1] ":" sprintf("%02d", int(t[2]/10)*10)
    cnt[bucket]++
}
END {
    printf "%-8s %6s\n", "TIME", "CRC"
    for (b in cnt) printf "%-8s %6d\n", b, cnt[b]
}' | sort || true

echo ""
echo "--- CRC_HOURLY_RATE ---"
# CRC errors per hour vs received packets per hour
# Build CRC pattern to avoid double-counting OnRxError+CRC_ERROR/RX_ERROR pairs
CRC_AWK_RE="$CRC_DETAIL_RE"
awk -v crc_re="$CRC_AWK_RE" '
$0 ~ crc_re {
    split($2, t, ":")
    h = t[1]
    crc[h]++
}
/MH-LoRa:? / {
    split($2, t, ":")
    h = t[1]
    rx[h]++
}
END {
    printf "%-6s %6s %6s %8s\n", "HOUR", "CRC", "RX", "RATE%"
    for (h in crc) {
        r = (h in rx && rx[h] > 0) ? crc[h] * 100.0 / rx[h] : 0
        printf "%-6s %6d %6d %8.1f\n", h, crc[h], (h in rx ? rx[h] : 0), r
    }
}' "$LOGFILE" | sort || true

echo ""
echo "--- CRC_CLUSTERS ---"
# CRC errors within 2s of each other = collision bursts
grep -E "$CRC_DETAIL_RE" "$LOGFILE" | awk '{
    split($2, t, ":")
    split(t[3], s, ".")
    ts = t[1] * 3600 + t[2] * 60 + s[1] + (length(s) > 1 ? ("0." s[2]) + 0 : 0)
    if (NR > 1 && ts - prev_ts < 2.0) {
        if (!in_cluster) { cluster_size = 2; cluster_start = prev_ts_str; in_cluster = 1 }
        else cluster_size++
    } else {
        if (in_cluster) {
            clusters++
            total_in_clusters += cluster_size
            if (cluster_size > max_cluster) max_cluster = cluster_size
            printf "CLUSTER at %s size=%d gap=%.1fs\n", cluster_start, cluster_size, prev_ts - cluster_start_ts
            in_cluster = 0
        }
    }
    if (!in_cluster) { cluster_start_ts = ts }
    prev_ts = ts
    prev_ts_str = $2
}
END {
    if (in_cluster) {
        clusters++
        total_in_clusters += cluster_size
        if (cluster_size > max_cluster) max_cluster = cluster_size
        printf "CLUSTER at %s size=%d\n", cluster_start, cluster_size
    }
    printf "\nCLUSTER_COUNT: %d\n", clusters+0
    printf "TOTAL_CRC_IN_CLUSTERS: %d\n", total_in_clusters+0
    printf "MAX_CLUSTER_SIZE: %d\n", max_cluster+0
}' || true

echo ""
echo "--- CRC_VS_CHANNEL_UTIL ---"
if [ "$BILLING_OVERSTATED" -eq 1 ]; then
    echo "NOTE: UTIL% is overstated (ESP32 build < f2d6532f bills rx as 255 B per frame); CRC and util not comparable"
fi
# Correlate CRC buckets with channel utilization buckets
awk -v crc_re="$CRC_AWK_RE" '
$0 ~ crc_re {
    split($2, t, ":")
    bucket = t[1] ":" sprintf("%02d", int(t[2]/10)*10)
    crc[bucket]++
}
/CHANNEL_UTIL/ {
    split($2, t, ":")
    bucket = t[1] ":" sprintf("%02d", int(t[2]/10)*10)
    for (i=1; i<=NF; i++) {
        if ($i ~ /^util=/) {
            util_sum[bucket] += substr($i, 6) + 0.0
            util_cnt[bucket]++
        }
    }
}
END {
    printf "%-8s %6s %8s\n", "TIME", "CRC", "UTIL%"
    for (b in crc) {
        u = (b in util_cnt && util_cnt[b] > 0) ? util_sum[b] / util_cnt[b] : -1
        if (u >= 0)
            printf "%-8s %6d %8.1f\n", b, crc[b], u
        else
            printf "%-8s %6d %8s\n", b, crc[b], "n/a"
    }
}' "$LOGFILE" | sort || true

# ─── 15. CAD ATTEMPT DISTRIBUTION ───
section "CAD_ATTEMPT_DISTRIBUTION"

grep "TX_GATE_ENTER" "$LOGFILE" | grep -oE 'cad_attempt=[0-9]+' | awk -F= '{
    cnt[$2]++
    total++
}
END {
    printf "%-12s %8s %8s\n", "ATTEMPT", "COUNT", "PCT%"
    max_a = 0
    for (a in cnt) { if (a+0 > max_a) max_a = a+0 }
    for (i = 0; i <= max_a; i++) {
        if (i in cnt) {
            printf "%-12d %8d %8.1f\n", i, cnt[i], cnt[i] * 100.0 / total
        }
    }
    printf "\nTOTAL_TX_ATTEMPTS: %d\n", total

    # Summary buckets
    free_first = cnt[0] + 0
    busy_1 = cnt[1] + 0
    busy_2 = cnt[2] + 0
    starvation = 0
    for (a in cnt) { if (a + 0 >= 3) starvation += cnt[a] }
    printf "SOFORT_FREI (attempt=0): %d (%.1f%%)\n", free_first, (total > 0 ? free_first * 100.0 / total : 0)
    printf "1x_BUSY (attempt=1): %d (%.1f%%)\n", busy_1, (total > 0 ? busy_1 * 100.0 / total : 0)
    printf "2x_BUSY (attempt=2): %d (%.1f%%)\n", busy_2, (total > 0 ? busy_2 * 100.0 / total : 0)
    printf "STARVATION (attempt>=3): %d (%.1f%%)\n", starvation, (total > 0 ? starvation * 100.0 / total : 0)
}' || true

# ─── 16. CAD STORM ANALYSIS ───
section "CAD_STORM"

# Extract CAD storm episodes: sequences where cad_attempt >= 3
# A storm starts at the first TX_GATE_ENTER with cad_attempt>=3 and ends at TX_DONE or cad_attempt=0
awk '
/TX_GATE_ENTER/ {
    split($2, t, ":")
    split(t[3], s, ".")
    ts = t[1] * 3600 + t[2] * 60 + s[1] + (length(s) > 1 ? ("0." s[2]) + 0 : 0)
    ts_str = $2

    for (i=1; i<=NF; i++) {
        if ($i ~ /^cad_attempt=/) {
            attempt = substr($i, 13) + 0
        }
    }

    if (attempt >= 3) {
        if (!in_storm) {
            in_storm = 1
            storm_start = ts
            storm_start_str = ts_str
            storm_max = attempt
            storm_scans = 1
        } else {
            storm_scans++
            if (attempt > storm_max) storm_max = attempt
        }
    }

    if (attempt == 0 && in_storm) {
        # Storm ended
        duration = ts - storm_start
        storms++
        dur_sum += duration
        if (storms == 1 || duration > dur_max) dur_max = duration
        if (storms == 1 || duration < dur_min) dur_min = duration
        durations[storms] = duration

        if (duration < 1) bucket_lt1++
        else if (duration < 2) bucket_1_2++
        else if (duration < 5) bucket_2_5++
        else if (duration < 10) bucket_5_10++
        else if (duration < 30) bucket_10_30++
        else bucket_gt30++

        printf "STORM at %s dur=%.1fs max_attempt=%d scans=%d\n", storm_start_str, duration, storm_max, storm_scans
        in_storm = 0
    }
}
/TX_DONE/ {
    if (in_storm) {
        split($2, t, ":")
        split(t[3], s, ".")
        ts = t[1] * 3600 + t[2] * 60 + s[1] + (length(s) > 1 ? ("0." s[2]) + 0 : 0)
        duration = ts - storm_start
        storms++
        dur_sum += duration
        if (storms == 1 || duration > dur_max) dur_max = duration
        if (storms == 1 || duration < dur_min) dur_min = duration
        durations[storms] = duration

        if (duration < 1) bucket_lt1++
        else if (duration < 2) bucket_1_2++
        else if (duration < 5) bucket_2_5++
        else if (duration < 10) bucket_5_10++
        else if (duration < 30) bucket_10_30++
        else bucket_gt30++

        printf "STORM at %s dur=%.1fs max_attempt=%d scans=%d (ended by TX_DONE)\n", storm_start_str, duration, storm_max, storm_scans
        in_storm = 0
    }
}
END {
    if (in_storm) {
        printf "STORM at %s OPEN (still active at log end) max_attempt=%d scans=%d\n", storm_start_str, storm_max, storm_scans
    }
    printf "\n--- STORM_SUMMARY ---\n"
    printf "TOTAL_STORMS: %d\n", storms+0
    if (storms > 0) {
        printf "DURATION_MIN: %.1fs\n", dur_min
        printf "DURATION_AVG: %.1fs\n", dur_sum / storms
        printf "DURATION_MAX: %.1fs\n", dur_max
        # Sort durations for median
        n = storms
        for (i=1; i<=n; i++) sorted[i] = durations[i]
        for (i=1; i<=n; i++) for (j=i+1; j<=n; j++) if (sorted[j] < sorted[i]) { tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp }
        if (n % 2 == 1) median = sorted[int(n/2)+1]
        else median = (sorted[n/2] + sorted[n/2+1]) / 2.0
        printf "DURATION_MEDIAN: %.1fs\n", median
    }
    printf "\n--- STORM_DURATION_HISTOGRAM ---\n"
    printf "<1s:    %d\n", bucket_lt1+0
    printf "1-2s:   %d\n", bucket_1_2+0
    printf "2-5s:   %d\n", bucket_2_5+0
    printf "5-10s:  %d\n", bucket_5_10+0
    printf "10-30s: %d\n", bucket_10_30+0
    printf ">30s:   %d\n", bucket_gt30+0

    printf "\n--- 500ms_RX_LOOP_ANALYSIS ---\n"
    if (storms > 0) {
        cost_500ms = storms * 0.5
        printf "STORMS_COUNT: %d\n", storms
        printf "TOTAL_STORM_TIME: %.1fs\n", dur_sum
        printf "COST_500ms_RX_LOOP: %.1fs (if added before each storm)\n", cost_500ms
        printf "NET_SAVINGS_IF_RX_LOOP_CATCHES_PKT: Depends on hit rate\n"
        printf "NOTE: A 500ms RX loop at storm start would detect preamble+header\n"
        printf "      and receive the packet (~2.5s), avoiding repeated CAD polls.\n"
        if (dur_sum > 0)
            printf "      Break-even: if >%.0f%% of storms have a receivable packet.\n", cost_500ms * 100.0 / dur_sum
        else
            printf "      Break-even: n/a (total storm time is 0 s)\n"
    }
}' "$LOGFILE" || true

# ─── 17. RING OVERFLOW ───
section "RING_OVERFLOW"

echo "--- RING_DROP_EVENTS ---"
RING_DROPS=$(grep -c 'RING_DROP' "$LOGFILE" 2>/dev/null || true)
echo "RING_DROP_COUNT: $RING_DROPS"
grep "RING_DROP" "$LOGFILE" || true

echo ""
echo "--- RING_OVERFLOW_BY_BUF ---"
# buf=phone and buf=raw_rx are suppressed in firmware since 898ba570.
grep -oE 'RING_OVERFLOW buf=[a-z_]+' "$LOGFILE" | sort | uniq -c | sort -rn || true

echo ""
echo "--- RING_WATERMARK ---"
grep "RING_STATUS" "$LOGFILE" | grep -oE 'queued=[0-9]+' | awk -F= '{
    v = $2 + 0
    total++
    if (total == 1 || v > max) max = v
    if (v == 0) q0++
    else if (v <= 5) q1_5++
    else if (v <= 10) q10++
    else if (v <= 15) q15++
    else q20++
    sum += v
}
END {
    printf "SAMPLES: %d\n", total+0
    printf "MAX_QUEUED: %d\n", max+0
    printf "AVG_QUEUED: %.1f\n", (total > 0 ? sum / total : 0)
    printf "\n--- WATERMARK_HISTOGRAM ---\n"
    printf "queued=0:     %6d (%5.1f%%)\n", q0+0, (total > 0 ? (q0+0)*100.0/total : 0)
    printf "queued=1-5:   %6d (%5.1f%%)\n", q1_5+0, (total > 0 ? (q1_5+0)*100.0/total : 0)
    printf "queued=6-10:  %6d (%5.1f%%)\n", q10+0, (total > 0 ? (q10+0)*100.0/total : 0)
    printf "queued=11-15: %6d (%5.1f%%)\n", q15+0, (total > 0 ? (q15+0)*100.0/total : 0)
    printf "queued=16-20: %6d (%5.1f%%)\n", q20+0, (total > 0 ? (q20+0)*100.0/total : 0)
}' || true

echo ""
echo "--- RING_WATERMARK_OVER_TIME ---"
grep "RING_STATUS" "$LOGFILE" | awk '{
    split($2, t, ":")
    bucket = t[1] ":" sprintf("%02d", int(t[2]/10)*10)
    for (i=1; i<=NF; i++) {
        if ($i ~ /^queued=/) {
            v = substr($i, 8) + 0
            sum[bucket] += v
            cnt[bucket]++
            if (!(bucket in mx) || v > mx[bucket]) mx[bucket] = v
        }
    }
}
END {
    printf "%-8s %8s %8s %5s\n", "TIME", "AVG_Q", "MAX_Q", "N"
    for (b in cnt) printf "%-8s %8.1f %8d %5d\n", b, sum[b]/cnt[b], mx[b], cnt[b]
}' | sort || true

# ─── 18. DROPPED PACKETS (comprehensive) ───
section "DROPPED_PACKETS"

echo "--- DROP_CATEGORIES ---"
RING_DROP_N=$(grep -c 'RING_DROP' "$LOGFILE" 2>/dev/null || true); echo "RING_DROP: ${RING_DROP_N:-0}"
CRC_ERR_ESP2=$(grep -c 'CRC_ERROR' "$LOGFILE" 2>/dev/null || true)
CRC_ERR_NRF_NEW2=$(grep -c 'RX_ERROR rssi' "$LOGFILE" 2>/dev/null || true)
CRC_ERR_NRF_OLD2=$(grep -c 'OnRxError$' "$LOGFILE" 2>/dev/null || true)
# Same dedup logic: OnRxError only when no structured detail lines exist
if [ "$CRC_ERR_ESP2" -gt 0 ] || [ "$CRC_ERR_NRF_NEW2" -gt 0 ]; then
    CRC_ERR_NRF2=$CRC_ERR_NRF_NEW2
else
    CRC_ERR_NRF2=$CRC_ERR_NRF_OLD2
fi
CRC_ERR_N=$((CRC_ERR_ESP2 + CRC_ERR_NRF2))
if [ "$CRC_SRC" = "setlog" ]; then CRC_ERR_N=$SL_ERR_N; fi
echo "CRC_ERROR: ${CRC_ERR_N:-0}"
DEDUP_N=$(grep -c 'RX_DEDUP_DUP' "$LOGFILE" 2>/dev/null || true); echo "RX_DEDUP_DUP: ${DEDUP_N:-0}"
LOOP_N=$(grep -c 'RELAY_LOOP_BLOCKED' "$LOGFILE" 2>/dev/null || true); echo "RELAY_LOOP_BLOCKED: ${LOOP_N:-0}"
RXOE_N=$(grep -c 'RX_OTHER_ERROR' "$LOGFILE" 2>/dev/null || true); echo "RX_OTHER_ERROR: ${RXOE_N:-0}"
RTGU_N=$(grep -c 'RETRANSMIT_GIVEUP retries=' "$LOGFILE" 2>/dev/null || true); echo "RETRANSMIT_GIVEUP: ${RTGU_N:-0}"
# Three distinct events (an unanchored RX_IRQ_STALE counted all of them,
# i.e. one stale IRQ twice: the rssi line plus its forced-restart line).
IRQS_N=$(grep -c 'RX_IRQ_STALE rssi=' "$LOGFILE" 2>/dev/null || true); echo "RX_IRQ_STALE: ${IRQS_N:-0}"
IRQE_N=$(grep -c 'RX_IRQ_STALE_EARLY' "$LOGFILE" 2>/dev/null || true); echo "RX_IRQ_STALE_EARLY: ${IRQE_N:-0}"
IRQF_N=$(grep -c 'RX_IRQ_STALE forced restart' "$LOGFILE" 2>/dev/null || true); echo "RX_IRQ_STALE_FORCED_RESTART: ${IRQF_N:-0}"

echo ""
echo "--- HOP_LIMIT_DROPS ---"
# Packets where relay was suppressed due to hop limit
# These show up as MH-LoRa but NOT RX-LoRa2 (heard but not accepted)
# We detect by counting relays in MH-LoRa lines. The route is
# originator,relay1,relay2,...>dest, so relays = commas before ">"
# (callsigns = relays + 1).
grep -E 'MH-LoRa:? ' "$LOGFILE" | awk '{
    for (i=1; i<=NF; i++) {
        if ($i ~ />/) {
            # Count commas in the path before ">"
            split($i, parts, ">")
            path = parts[1]
            relays = gsub(/,/, ",", path)
            if (relays > 3) {
                high_path++
                # Extract msg_id (xHEXHEXHEX)
                for (j=1; j<=NF; j++) {
                    if ($j ~ /^x[A-Fa-f0-9]{8}$/) { mid = $j; break }
                }
                for (j=1; j<=NF; j++) {
                    if ($j ~ /^H[0-9A-F][0-9A-F]$/) { hop = $j; break }
                }
            }
            path_dist[relays]++
            if (relays > maxp) maxp = relays
            break
        }
    }
}
BEGIN { maxp = -1 }
END {
    # Rows in relay order (0..max) straight from awk: an outer `sort -n`
    # interleaved the header and trailer lines with the relays=0 row.
    printf "--- PATH_LENGTH_DISTRIBUTION ---\n"
    printf "%-8s %8s\n", "RELAYS", "COUNT"
    for (p = 0; p <= maxp; p++) if (p in path_dist) printf "%-8d %8d\n", p, path_dist[p]
    printf "\nRELAYS_GT_3: %d\n", high_path+0
}' || true

echo ""
echo "--- ALL_DROPS_SAMPLES ---"
grep -E 'RING_DROP|RX_OTHER_ERROR|RELAY_LOOP_BLOCKED|RX_IRQ_STALE( rssi=|_EARLY| forced)|RETRANSMIT_GIVEUP' "$LOGFILE" | head -30 || true

# ─── 19. HIGH HOP PACKETS ───
section "HIGH_HOP_PACKETS"

# List all packets with more than 3 relays (originator + >3 relays in the route)
grep -E 'MH-LoRa:? ' "$LOGFILE" | awk '{
    ptype = ""
    for (i=1; i<=NF; i++) {
        if ($i ~ /^MH-LoRa:?$/) { ptype = $(i+2); break }
    }
    for (i=1; i<=NF; i++) {
        if ($i ~ />/) {
            split($i, parts, ">")
            path = parts[1]
            relays = gsub(/,/, ",", path)
            if (relays > 3) {
                # Reconstruct key info
                mid = "?"
                hop = "?"
                for (j=1; j<=NF; j++) {
                    if ($j ~ /^x[A-Fa-f0-9]{8}$/) { mid = $j; break }
                }
                for (j=1; j<=NF; j++) {
                    if ($j ~ /^H[0-9A-F][0-9A-F]$/) { hop = $j; break }
                }
                # payload type = field after the 3-digit length (printBuffer_aprs %c)
                type = "OTHER"
                if (ptype == "@") type = "HB"
                else if (ptype == "!") type = "POS"
                else if (ptype == ":") type = "TXT"
                printf "%s %s relays=%d hop_remain=%s type=%s path=%s\n", $2, mid, relays, hop, type, $i
            }
            break
        }
    }
}' || true

echo ""
echo "--- PATH_LENGTH_SUMMARY ---"
grep -E 'MH-LoRa:? ' "$LOGFILE" | awk '{
    for (i=1; i<=NF; i++) {
        if ($i ~ />/) {
            split($i, parts, ">")
            path = parts[1]
            relays = gsub(/,/, ",", path)
            dist[relays]++
            total++
            if (relays > maxp) maxp = relays
            break
        }
    }
}
BEGIN { maxp = -1 }
END {
    printf "%-8s %8s %8s\n", "RELAYS", "COUNT", "PCT%"
    for (p = 0; p <= maxp; p++) if (p in dist) printf "%-8d %8d %8.1f\n", p, dist[p], dist[p] * 100.0 / total
    printf "TOTAL: %d\n", total
}' || true

# ─── 20. CROSS-LOG CORRELATION ───
if [ $# -ge 2 ] && [ -f "$2" ]; then
    INPUT2="$2"
    normalize_log "$2"
    LOGFILE2="$NORM_RESULT"
    section "CROSS_CORRELATION"

    echo "LOG1: $INPUT1"
    echo "LOG2: $INPUT2"

    # Extract msg_ids and timestamps from MH-LoRa lines in both logs
    new_tmp; XLOG1="$NEW_TMP"
    new_tmp; XLOG2="$NEW_TMP"
    grep -E 'MH-LoRa:? ' "$LOGFILE" | awk '{
        for (i=1; i<=NF; i++) {
            if ($i ~ /^x[A-Fa-f0-9]{8}$/) { print $i, $2; break }
        }
    }' | sort -u -k1,1 > "$XLOG1"

    grep -E 'MH-LoRa:? ' "$LOGFILE2" | awk '{
        for (i=1; i<=NF; i++) {
            if ($i ~ /^x[A-Fa-f0-9]{8}$/) { print $i, $2; break }
        }
    }' | sort -u -k1,1 > "$XLOG2"

    LOG1_TOTAL=$(wc -l < "$XLOG1" | tr -d ' ')
    LOG2_TOTAL=$(wc -l < "$XLOG2" | tr -d ' ')

    # Common msg_ids
    COMMON=$(comm -12 <(awk '{print $1}' "$XLOG1" | sort) <(awk '{print $1}' "$XLOG2" | sort) | wc -l | tr -d ' ')
    ONLY_LOG1=$(comm -23 <(awk '{print $1}' "$XLOG1" | sort) <(awk '{print $1}' "$XLOG2" | sort) | wc -l | tr -d ' ')
    ONLY_LOG2=$(comm -13 <(awk '{print $1}' "$XLOG1" | sort) <(awk '{print $1}' "$XLOG2" | sort) | wc -l | tr -d ' ')

    echo "LOG1_UNIQUE_MSGIDS: $LOG1_TOTAL"
    echo "LOG2_UNIQUE_MSGIDS: $LOG2_TOTAL"
    echo "COMMON_MSGIDS: $COMMON"
    echo "ONLY_IN_LOG1: $ONLY_LOG1"
    echo "ONLY_IN_LOG2: $ONLY_LOG2"

    echo ""
    echo "--- RECEPTION_ASYMMETRY ---"
    if [ "$LOG1_TOTAL" -gt 0 ]; then
        echo "LOG1_RECEPTION_RATE: $(echo "scale=1; $COMMON * 100 / $LOG1_TOTAL" | bc)% of LOG1 also heard by LOG2"
    fi
    if [ "$LOG2_TOTAL" -gt 0 ]; then
        echo "LOG2_RECEPTION_RATE: $(echo "scale=1; $COMMON * 100 / $LOG2_TOTAL" | bc)% of LOG2 also heard by LOG1"
    fi

    echo ""
    echo "--- RELAY_DELAY ---"
    # For common msg_ids, compute time difference between first reception in each log
    join -j 1 <(sort -k1,1 "$XLOG1") <(sort -k1,1 "$XLOG2") | awk '{
        # $1=msg_id, $2=ts_log1 (HH:MM:SS.mmm), $3=ts_log2 (HH:MM:SS.mmm)
        split($2, t1, ":")
        split(t1[3], s1, ".")
        sec1 = t1[1] * 3600 + t1[2] * 60 + s1[1] + (length(s1) > 1 ? ("0." s1[2]) + 0 : 0)

        split($3, t2, ":")
        split(t2[3], s2, ".")
        sec2 = t2[1] * 3600 + t2[2] * 60 + s2[1] + (length(s2) > 1 ? ("0." s2[2]) + 0 : 0)

        diff = sec2 - sec1
        if (diff < 0) diff = -diff
        sum += diff
        count++
        if (count == 1 || diff > max) max = diff
        if (count == 1 || diff < min) min = diff
        if (diff < 1) d_lt1++
        else if (diff < 5) d_1_5++
        else if (diff < 10) d_5_10++
        else d_gt10++
    }
    END {
        if (count > 0) {
            printf "SAMPLES: %d\n", count
            printf "DELAY_MIN: %.1fs\n", min
            printf "DELAY_AVG: %.1fs\n", sum / count
            printf "DELAY_MAX: %.1fs\n", max
            printf "\n--- DELAY_HISTOGRAM ---\n"
            printf "<1s:   %d\n", d_lt1+0
            printf "1-5s:  %d\n", d_1_5+0
            printf "5-10s: %d\n", d_5_10+0
            printf ">10s:  %d\n", d_gt10+0
        } else {
            print "NO_COMMON_PACKETS"
        }
    }' || true

    echo ""
    echo "--- ONLY_IN_LOG1_SAMPLES ---"
    comm -23 <(awk '{print $1}' "$XLOG1" | sort) <(awk '{print $1}' "$XLOG2" | sort) | head -20 || true

    echo ""
    echo "--- ONLY_IN_LOG2_SAMPLES ---"
    comm -13 <(awk '{print $1}' "$XLOG1" | sort) <(awk '{print $1}' "$XLOG2" | sort) | head -20 || true

    # Temp files are removed by the EXIT trap.
fi

# ─── PRIORITY DISTRIBUTION ───
section "PRIORITY_DISTRIBUTION"
if grep -q '\[MC-STAT\]' "$LOGFILE"; then
    echo "--- TX counts per priority (from [MC-STAT] lines) ---"
    grep '\[MC-STAT\]' "$LOGFILE" | tail -20
    # F-23: each [MC-STAT] line is followed by two untagged continuation
    # lines ("tx: p1=.. preempt=", "drop: p1=.."), possibly behind a
    # timestamp. Pair only the lines directly after the [MC-STAT] line.
    awk '
    function grab(line, key,    k, n, a, i) {
        n = split(line, a, " ")
        for (i = 1; i <= n; i++)
            if (a[i] ~ /^(p[1-5]|preempt)=[0-9]+$/) {
                k = a[i]; sub(/=.*/, "", k)
                if (key == "tx") tx[k] += substr(a[i], length(k) + 2) + 0
                else dr[k] += substr(a[i], length(k) + 2) + 0
            }
    }
    /\[MC-STAT\]/ { if (cur != "") raw[++nblk] = cur; blocks++; want = 2; cur = $0; next }
    want > 0 {
        if ($0 ~ /(^|[ \t])tx: p1=/) { grab($0, "tx"); cur = cur "\n" $0; want--; next }
        if ($0 ~ /(^|[ \t])drop: p1=/) { grab($0, "drop"); cur = cur "\n" $0; want--; next }
        want = 0
    }
    { if (want == 0 && cur != "") { raw[++nblk] = cur; cur = "" } }
    END {
        if (cur != "") raw[++nblk] = cur
        if (blocks == 0) exit
        printf "MC_STAT_BLOCKS: %d\n", blocks
        printf "MC_STAT_TX_TOTAL: p1=%d p2=%d p3=%d p4=%d p5=%d preempt=%d\n", tx["p1"], tx["p2"], tx["p3"], tx["p4"], tx["p5"], tx["preempt"]
        printf "MC_STAT_DROP_TOTAL: p1=%d p2=%d p3=%d p4=%d p5=%d\n", dr["p1"], dr["p2"], dr["p3"], dr["p4"], dr["p5"]
        print "--- last [MC-STAT] blocks (tx/drop continuation lines) ---"
        for (i = (nblk > 3 ? nblk - 2 : 1); i <= nblk; i++) print raw[i]
    }' "$LOGFILE"
    echo ""
    echo "--- Latency per priority (from [MC-PRIO] lines) ---"
    grep '\[MC-PRIO\]' "$LOGFILE" | tail -20
    echo ""
    echo "--- Priority drops ---"
    grep 'RING_DROP_PRIO\|RING_DROP_NEW' "$LOGFILE" | wc -l | awk '{printf "  Total priority drops: %d\n", $1}'
    echo "--- Dropped packets by prio+type ---"
    (grep -E 'RING_DROP_(PRIO|NEW)' "$LOGFILE" | grep -oE 'prio=[0-9]+ type=[0-9A-Fa-f]+' | sort | uniq -c | sort -rn) || true
else
    echo "  No [MC-STAT] data found (priority queue not active or no data yet)"
fi

# ─── TRICKLE HEY ───
section "TRICKLE_HEY"
if grep -q '\[MC-TRICKLE\]' "$LOGFILE"; then
    echo "--- Trickle-HEY events ---"
    trickle_send=$(grep -c 'MC-TRICKLE.*SEND' "$LOGFILE" || true)
    trickle_suppress=$(grep -c 'MC-TRICKLE.*SUPPRESS' "$LOGFILE" || true)
    trickle_topo=$(grep -c 'MC-TRICKLE.*TOPO_CHANGE' "$LOGFILE" || true)
    trickle_total=$((trickle_send + trickle_suppress))
    if [ "$trickle_total" -gt 0 ]; then
        suppress_pct=$((trickle_suppress * 100 / trickle_total))
    else
        suppress_pct=0
    fi
    echo "  HEY sent: $trickle_send"
    echo "  HEY suppressed: $trickle_suppress ($suppress_pct%)"
    echo "  Topology changes: $trickle_topo"
    echo ""
    echo "--- Last 10 Trickle events ---"
    grep '\[MC-TRICKLE\]' "$LOGFILE" | tail -10
else
    echo "  No [MC-TRICKLE] data found (Trickle-HEY not active or no data yet)"
fi

# ─── HIGH WATER MARKS ───
section "HIGH_WATER_MARKS"
if grep -q '\[MC-HWM\]' "$LOGFILE"; then
    echo "--- High-Water Marks ---"
    grep '\[MC-HWM\]' "$LOGFILE" | tail -5
elif grep -q '\[LOG\] STAT' "$LOGFILE"; then
    echo "  [MC-HWM] n/a (setlog on: see STAT section ringmax)"
else
    echo "  No [MC-HWM] data found"
fi

# ─── CONSOLIDATED ADVANCED ANALYSIS (Pass A) ───
# Single awk pass over entire log for: HEAP, Starvation, Server, NTP,
# Signal Profiling, Frequency Drift, Interferers, Top Talkers,
# Channel Util Diagram, CSMA Timing, ACK Storm, CAD enhancements
awk -v have_rcv="$SL_HAVE_RCV" -v mh_synth="$MH_SYNTH" -v payload_re="$PAYLOAD_LINE_RE" '
BEGIN {
    # HEAP
    heap_cnt = 0; heap_sum = 0; heap_sumsq = 0
    heap_min = 999999999; heap_max = 0
    heap_mon_cnt = 0; heap_ext_cnt = 0; heap_lwm = 999999999; heap_mab = 0; heap_mab_min = 999999999
    heap_prev = 0; heap_mono_dec = 0; heap_mono_max = 0
    heap_first = 0; heap_first_ts = ""
    psrm_val = ""

    # Starvation
    last_rxfire_sec = -1; silent_cnt = 0
    last_sm_sec = -1; last_sm_state = ""; stuck_cnt = 0
    zombie_streak = 0; zombie_cnt = 0
    prio1_starve_cnt = 0

    # Server
    keep_cnt = 0; last_keep_sec = -1; keep_gap_cnt = 0
    wifi_gotip = 0; wifi_connect_legacy = 0
    wifi_disc_event = 0; wifi_disc_legacy = 0
    wifi_stall = 0; udp_reset = 0

    # NTP
    ntp_cnt = 0; ntp_prev_sec = -1

    # Signal profiling (per direct-heard node H00)
    # Uses sig_rssi_sum[cs], sig_rssi_sumsq[cs], sig_rssi_cnt[cs],
    #      sig_rssi_min[cs], sig_rssi_max[cs],
    #      sig_snr_sum[cs], sig_snr_sumsq[cs], sig_snr_cnt[cs],
    #      sig_snr_min[cs], sig_snr_max[cs],
    #      sig_ferr_sum[cs], sig_ferr_sumsq[cs], sig_ferr_cnt[cs],
    #      sig_ferr_min[cs], sig_ferr_max[cs]
    # Freq drift: sig_ferr_day_sum[cs], sig_ferr_day_cnt[cs],
    #             sig_ferr_night_sum[cs], sig_ferr_night_cnt[cs]
    # Linear regression: sig_ferr_tx_sum[cs], sig_ferr_txx_sum[cs],
    #                    sig_ferr_txy_sum[cs] (t = hour since start)

    last_rx_rssi = 0; last_rx_snr = 0; last_rx_ferr = 0; has_rx = 0
    last_rx_hour = 0
    rcv_pending = 0; ferr_ok = 1

    # Top talkers: heard[cs]; TX counters (tx_srv0/tx_srv1/tx_total) below
    # S0/S1 in a packet line is the msg_server (server) flag, NOT own-vs-relay.
    tx_srv0 = 0; tx_srv1 = 0; tx_total = 0; tx_ack_frames = 0; relay_q_cnt = 0

    # Channel util diagram: chutil_hour_sum[hh], chutil_hour_cnt[hh]
    # CSMA timing: wait histogram
    wait_lt3 = 0; wait_3_4 = 0; wait_4_5 = 0; wait_5_6 = 0; wait_gt6 = 0
    wait_sum = 0; wait_cnt = 0; wait_min = 999999; wait_max = 0

    # Inter-arrival: ia_sum, ia_cnt, ia_min, ia_max
    last_rx_sec = -1; ia_sum = 0; ia_cnt = 0; ia_min = 999999; ia_max = 0

    # TX gate
    tx_gate_cnt = 0

    # CAD enhanced
    # cad_busy1_cnt = CAD_BUSY_1 (initial busy), cad_busy_cnt = "CAD_BUSY attempt="
    # (confirmed busy after the double-check)
    cad_busy1_cnt = 0; cad_busy_cnt = 0; cad_fp_cnt = 0; cad_abort_rx = 0
    cad_safety_cnt = 0
    # CAD per hour: cad_busy_hour[hh], cad_fp_hour[hh]

    # ACK
    ack_cnt = 0; ack_received = 0; ack_skip = 0; ack_reject = 0
    ack_appendix = 0

    # Interferer
    interf_cnt = 0

    # First timestamp for time calculations
    start_sec = -1
}

function parse_sec(ts,    parts, hms) {
    # Parse HH:MM:SS.mmm or HH:MM:SS to seconds
    split(ts, hms, ":")
    return hms[1]*3600 + hms[2]*60 + hms[3]+0
}

function parse_hour(ts,    hms) {
    split(ts, hms, ":")
    return hms[1]+0
}

function abs(v) { return v < 0 ? -v : v }

function sqrt_approx(x,    g, i) {
    if (x <= 0) return 0
    g = x / 2
    for (i = 0; i < 20; i++) g = (g + x/g) / 2
    return g
}

function stddev(sum, sumsq, n) {
    if (n < 2) return 0
    return sqrt_approx((sumsq - sum*sum/n) / (n-1))
}

# ── Parse serial-monitor timestamp from every line ──
{
    line_ts = $2  # HH:MM:SS.mmm
    line_sec = parse_sec(line_ts)
    line_hour = parse_hour(line_ts)
    if (start_sec < 0) start_sec = line_sec
}

# ── HEAP ──
# Three forms (all: free, min_free, max_alloc, tag after the "[HEAP]" tag):
#   csv   [HEAP];HH:MM:SS;free;min;maxalloc;(mon|init)   (--debug csv)
#   space [HEAP] HH:MM:SS free min maxalloc (mon|init)   (default ESP32)
#   nRF52 HH:MM:SS;[HEAP];free;min;maxalloc;(mon)        (Serial.printf)
# "[HEAP]Cnew" / "[HEAP]Cdis" (web clients) have no separator and are skipped.
/\[HEAP\][; ]/ {
    hrest = $0
    sub(/^.*\[HEAP\][; ]+/, "", hrest)
    n = split(hrest, parts, /[; ]+/)
    hk = 1
    if (parts[1] ~ /^[0-9][0-9]:[0-9][0-9]:[0-9][0-9]$/) hk = 2
    if (n >= hk) {
        val = parts[hk] + 0
        if (val > 0) {
            heap_cnt++
            heap_sum += val
            heap_sumsq += val * val
            if (val < heap_min) heap_min = val
            if (val > heap_max) heap_max = val
            if (heap_cnt == 1) { heap_first = val; heap_first_ts = line_ts }
            heap_last = val; heap_last_ts = line_ts

            # Hourly bucket
            heap_hour_sum[line_hour] += val
            heap_hour_cnt[line_hour]++
            if (!(line_hour in heap_hour_min) || val < heap_hour_min[line_hour])
                heap_hour_min[line_hour] = val

            # Monotonic decrease detection (steady state: after first 30 samples)
            if (heap_cnt > 30) {
                if (val < heap_prev) {
                    heap_mono_dec++
                    if (heap_mono_dec > heap_mono_max) heap_mono_max = heap_mono_dec
                } else if (val > heap_prev) {
                    heap_mono_dec = 0
                }
            }
            heap_prev = val

            # parts[hk+1] = min_free (low watermark), parts[hk+2] = largest
            # allocatable block (NOT a high watermark), parts[hk+3] = tag.
            if (n >= hk + 3) {
                heap_ext_cnt++
                if (parts[hk+3] ~ /mon/) heap_mon_cnt++
                lwm = parts[hk+1] + 0
                mab_val = parts[hk+2] + 0
                if (lwm > 0 && lwm < heap_lwm) heap_lwm = lwm
                if (mab_val > 0) {
                    if (mab_val > heap_mab) heap_mab = mab_val
                    if (mab_val < heap_mab_min) heap_mab_min = mab_val
                }
            }
        }
    }
}

/\[PSRM\]/ {
    n = split($0, parts, ";")
    if (n >= 3) psrm_val = parts[3]
}

# ── Starvation: RX_TIMEOUT_FIRE ──
/RX_TIMEOUT_FIRE/ {
    if (last_rxfire_sec >= 0) {
        gap = line_sec - last_rxfire_sec
        # Handle day wrap
        if (gap < 0) gap += 86400
        if (gap > 20) {
            silent_cnt++
            if (silent_cnt <= 20) {
                silent_time[silent_cnt] = line_ts
                silent_gap[silent_cnt] = gap
            }
        }
    }
    last_rxfire_sec = line_sec

    # CSMA wait time extraction
    if (match($0, /wait=[0-9]+/)) {
        w = substr($0, RSTART+5, RLENGTH-5) + 0
        wait_cnt++
        wait_sum += w
        if (w < wait_min) wait_min = w
        if (w > wait_max) wait_max = w
        if (w < 3000) wait_lt3++
        else if (w < 4000) wait_3_4++
        else if (w < 5000) wait_4_5++
        else if (w < 6000) wait_5_6++
        else wait_gt6++
    }
}

# ── Starvation: MC-SM state transitions ──
/\[MC-SM\]/ {
    # Extract source and dest state
    if (match($0, /\[MC-SM\] [A-Z_]+ -> [A-Z_]+/)) {
        sm_str = substr($0, RSTART+7, RLENGTH-7)
        split(sm_str, sm_parts, " -> ")
        src_state = sm_parts[1]
        dst_state = sm_parts[2]

        if (last_sm_sec >= 0 && last_sm_state != "RX_LISTEN") {
            dur = line_sec - last_sm_sec
            if (dur < 0) dur += 86400
            if (dur > 30) {
                stuck_cnt++
                if (stuck_cnt <= 10) {
                    stuck_time[stuck_cnt] = line_ts
                    stuck_state[stuck_cnt] = last_sm_state
                    stuck_dur[stuck_cnt] = dur
                }
            }
        }
        last_sm_sec = line_sec
        last_sm_state = dst_state
    }
}

# ── Starvation: Ring zombies ──
# BP-02: queued= in RING_STATUS is now the honest occupied-slot count
# (was raw index distance iWrite-iRead, which counted holes freed behind a
# priority-starved entry as still queued -- DJ8MEH-RCA 2026-08-31). The
# zombie condition needs "ring actually drained", so prefer the new dist=
# field (the old index-distance value, unaffected by the redefinition) when
# present; fall back to queued==0 only for logs with no dist= field at all
# (firmware built before BP-02).
/RING_STATUS/ {
    retrying = 0; queued = 0; dist = -1
    if (match($0, /retrying=[0-9]+/))
        retrying = substr($0, RSTART+9, RLENGTH-9) + 0
    if (match($0, /queued=[0-9]+/))
        queued = substr($0, RSTART+7, RLENGTH-7) + 0
    if (match($0, /dist=[0-9]+/))
        dist = substr($0, RSTART+5, RLENGTH-5) + 0
    ring_drained = (dist >= 0) ? (dist == 0) : (queued == 0)
    if (retrying > 0 && ring_drained) {
        zombie_streak++
        if (zombie_streak == 3) {
            zombie_cnt++
            if (zombie_cnt <= 10) zombie_time[zombie_cnt] = line_ts
        }
    } else {
        zombie_streak = 0
    }
}

# ── Starvation: Prio1 ──
/\[MC-PRIO\]/ {
    if (match($0, /p1_lat_avg=[0-9]+/)) {
        lat = substr($0, RSTART+11, RLENGTH-11) + 0
        if (lat > 10000) {
            prio1_starve_cnt++
            if (prio1_starve_cnt <= 10) prio1_time[prio1_starve_cnt] = line_ts
        }
    }
}

# ── Server: KEEP heartbeats ──
/\[KEEP\]/ {
    keep_cnt++
    if (last_keep_sec >= 0) {
        kgap = line_sec - last_keep_sec
        if (kgap < 0) kgap += 86400
        if (kgap > 90) {
            keep_gap_cnt++
            if (keep_gap_cnt <= 10) {
                keep_gap_time[keep_gap_cnt] = line_ts
                keep_gap_dur[keep_gap_cnt] = kgap
            }
        }
    }
    last_keep_sec = line_sec
}

# ── Server: WiFi events ──
# Current firmware: [WIFI];event;got_ip / [WIFI];event;disconnected /
# [WIFI];stall (src/udp_functions.cpp). "connect OK" and "[WIFI]...disconnect"
# are the legacy human-readable forms (used only when no event lines exist).
# "Client disconnected" is the web/2323 client and is not a WiFi event.
/\[WIFI\];event;got_ip/ { wifi_gotip++ }
/\[WIFI\].*connect OK/ { wifi_connect_legacy++ }
/\[WIFI\];event;disconnected/ { wifi_disc_event++ }
/\[WIFI\]\.\.\..*disconnect/ { wifi_disc_legacy++ }
/\[WIFI\];stall/ { wifi_stall++ }
# UDP resets: resetMeshComUDP() (src/udp_functions.cpp) prints exactly one
# "[WIFI-DBG] resetMeshComUDP: WiFi disconnected, ..." line per call, whichever
# caller triggered it. The caller-side lines ("calling resetMeshComUDP",
# "[UDP] ... resetting") describe the same reset and are not counted again.
# Packet lines (payload text) never count.
/resetMeshComUDP:/ && $0 !~ payload_re { udp_reset++ }

# ── NTP ──
/TimeClient now/ {
    ntp_cnt++
    if (ntp_cnt <= 200) ntp_time[ntp_cnt] = line_ts
    if (ntp_prev_sec >= 0) {
        ndiff = line_sec - ntp_prev_sec
        if (ndiff < 0) ndiff += 86400
        ntp_interval_sum += ndiff
        ntp_interval_cnt++
    }
    ntp_prev_sec = line_sec
}

# ── Received packet: signal data ──
/Received packet:/ {
    last_rx_rssi = 0; last_rx_snr = 0; last_rx_ferr = 0; has_rx = 1
    for (i = 1; i <= NF; i++) {
        if ($i == "RSSI:") {
            for (j = i+1; j <= NF; j++) {
                if ($j ~ /^-?[0-9]/) { last_rx_rssi = $j + 0; break }
            }
        }
        if ($i == "SNR:") {
            for (j = i+1; j <= NF; j++) {
                if ($j ~ /^-?[0-9]/) { last_rx_snr = $j + 0; break }
            }
        }
    }
    # Frequency error
    if (match($0, /Frequency error:[[:space:]]+[-0-9.]+/)) {
        s = substr($0, RSTART, RLENGTH)
        gsub(/Frequency error:[[:space:]]+/, "", s)
        last_rx_ferr = s + 0
    }
    last_rx_hour = line_hour

    # Inter-arrival time
    if (last_rx_sec >= 0) {
        ia = line_sec - last_rx_sec
        if (ia < 0) ia += 86400
        if (ia < 300) {  # ignore gaps > 5min (likely log gaps)
            ia_cnt++
            ia_sum += ia
            if (ia < ia_min) ia_min = ia
            if (ia > ia_max) ia_max = ia
        }
    }
    last_rx_sec = line_sec
    rcv_pending = 1
    ferr_ok = 1
}

# ── SETLOG RX tail (`[LOG] 082 @ xID ... RSSI:-54 SNR:5 DUP:d OWN:e t=N`) ──
# Primary RSSI/SNR source whenever present (the only one on nRF52). It
# overrides the values of a preceding "Received packet:" line of the same
# frame, so a packet is never counted twice; ferr/inter-arrival keep coming
# from "Received packet:" and are skipped (ferr_ok = 0) when it is absent.
/\[LOG\] [0-9][0-9][0-9] / && / RSSI:-?[0-9]+ SNR:-?[0-9]+ DUP:/ {
    for (i = NF; i >= 1; i--) {
        if ($i ~ /^SNR:-?[0-9]+$/) { last_rx_snr = substr($i, 5) + 0 }
        if ($i ~ /^RSSI:-?[0-9]+$/) { last_rx_rssi = substr($i, 6) + 0; break }
    }
    ferr_ok = rcv_pending
    if (!rcv_pending) {
        last_rx_ferr = 0
        last_rx_hour = line_hour
        if (!have_rcv) {
            if (last_rx_sec >= 0) {
                ia = line_sec - last_rx_sec
                if (ia < 0) ia += 86400
                if (ia < 300) {
                    ia_cnt++; ia_sum += ia
                    if (ia < ia_min) ia_min = ia
                    if (ia > ia_max) ia_max = ia
                }
            }
            last_rx_sec = line_sec
        }
    }
    has_rx = 1
    rcv_pending = 0
}

# ── MH-LoRa: per-node signal profiling + top talkers ──
# Tag is "MH-LoRa " (since c78adcc5) or legacy "MH-LoRa:".
/MH-LoRa:? / {
    cs = ""
    is_direct = 0
    pkt_size = 0
    for (i = 1; i <= NF; i++) {
        if ($i ~ />/) {
            split($i, r, /[,>]/)
            cs = r[1]
            break
        }
    }
    if (cs == "" || cs ~ /^[0-9]/) next

    # Check hop count for direct (H00)
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^H[0-9][0-9]$/) {
            if ($i == "H00") is_direct = 1
            break
        }
    }

    # Packet size (first 3-digit field: the one after the tag)
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^[0-9][0-9][0-9]$/) { pkt_size = $i + 0; break }
    }

    # Top talkers
    heard[cs]++
    heard_size[cs] += pkt_size

    # Signal profiling for direct-heard nodes
    if (is_direct && has_rx) {
        sig_rssi_cnt[cs]++
        sig_rssi_sum[cs] += last_rx_rssi
        sig_rssi_sumsq[cs] += last_rx_rssi * last_rx_rssi
        if (!(cs in sig_rssi_min) || last_rx_rssi < sig_rssi_min[cs])
            sig_rssi_min[cs] = last_rx_rssi
        if (!(cs in sig_rssi_max) || last_rx_rssi > sig_rssi_max[cs])
            sig_rssi_max[cs] = last_rx_rssi

        sig_snr_cnt[cs]++
        sig_snr_sum[cs] += last_rx_snr
        sig_snr_sumsq[cs] += last_rx_snr * last_rx_snr
        if (!(cs in sig_snr_min) || last_rx_snr < sig_snr_min[cs])
            sig_snr_min[cs] = last_rx_snr
        if (!(cs in sig_snr_max) || last_rx_snr > sig_snr_max[cs])
            sig_snr_max[cs] = last_rx_snr

        # SETLOG: [LOG] RX tails carry no frequency error; only count ferr
        # when a "Received packet:" line fed this packet (ferr_ok).
        if (ferr_ok) {
        sig_ferr_cnt[cs]++
        sig_ferr_sum[cs] += last_rx_ferr
        sig_ferr_sumsq[cs] += last_rx_ferr * last_rx_ferr
        if (!(cs in sig_ferr_min) || last_rx_ferr < sig_ferr_min[cs])
            sig_ferr_min[cs] = last_rx_ferr
        if (!(cs in sig_ferr_max) || last_rx_ferr > sig_ferr_max[cs])
            sig_ferr_max[cs] = last_rx_ferr

        # Day/night freq drift (day=06-17, night=18-05)
        if (last_rx_hour >= 6 && last_rx_hour < 18) {
            sig_ferr_day_sum[cs] += last_rx_ferr
            sig_ferr_day_cnt[cs]++
        } else {
            sig_ferr_night_sum[cs] += last_rx_ferr
            sig_ferr_night_cnt[cs]++
        }

        # Linear regression: t = hours since start
        t_hrs = (line_sec - start_sec) / 3600
        if (t_hrs < 0) t_hrs += 24
        sig_ferr_tx_sum[cs] += t_hrs
        sig_ferr_txx_sum[cs] += t_hrs * t_hrs
        sig_ferr_txy_sum[cs] += t_hrs * last_rx_ferr

        # Interferer detection
        if (abs(last_rx_ferr) > 5000) {
            interf_cnt++
            if (interf_cnt <= 20) {
                interf_time[interf_cnt] = line_ts
                interf_cs[interf_cnt] = cs
                interf_ferr[interf_cnt] = last_rx_ferr
                interf_rssi[interf_cnt] = last_rx_rssi
                interf_snr[interf_cnt] = last_rx_snr
            }
        }
        }  # ferr_ok

        has_rx = 0
    }
}

# ── TX-LoRa: TX tracking ("TX-LoRa  075 ..."; legacy "TX-LoRa: 075 ...") ──
# ACK frames are "TX-Lora " (lowercase) and counted separately.
/TX-Lora +[0-9][0-9][0-9] / { tx_ack_frames++ }
/TX-LoRa:? +[0-9][0-9][0-9] / {
    tx_total++
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^S[01]$/) {
            if ($i == "S0") tx_srv0++
            else tx_srv1++
            break
        }
    }
    # Packet size for airtime
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^[0-9][0-9][0-9]$/) { tx_total_size += $i + 0; break }
    }
}

/RELAY_QUEUED/ { relay_q_cnt++ }

# ── Channel utilization ──
/CHANNEL_UTIL/ {
    if (match($0, /util=[0-9]+/)) {
        u = substr($0, RSTART+5, RLENGTH-5) + 0
        chutil_hour_sum[line_hour] += u
        chutil_hour_cnt[line_hour]++
    }
}

# ── CAD enhanced ──
/CAD_BUSY_1/ { cad_busy1_cnt++; cad_busy1_hour[line_hour]++ }
/CAD_BUSY attempt=/ { cad_busy_cnt++; cad_busy_hour[line_hour]++ }
/CAD_FALSE_POSITIVE/ { cad_fp_cnt++; cad_fp_hour[line_hour]++ }
/CAD_ABORT_BY_RX/ { cad_abort_rx++ }
/CAD_SAFETY_TIMEOUT/ { cad_safety_cnt++ }

# ── TX gate ──
/TX_GATE_ENTER/ { tx_gate_cnt++; tx_gate_hour[line_hour]++ }

# ── ACK events ──
/ACK_RECEIVED retid=/ { ack_received++; ack_cnt++ }
/ACK_SKIP_READY slot=/ { ack_skip++; ack_cnt++ }
/ACK_REJECT b5=/ { ack_reject++; ack_cnt++ }
/ACK_APPENDIX hash=/ { ack_appendix++; ack_cnt++ }

# ── BLE TX Latency: track user_msg path ──
/RING_WRITE.*src=user_msg/ {
    if (match($0, /msg_id=[0-9A-Fa-f]+/)) {
        bmid = substr($0, RSTART+7, RLENGTH-7)
        ble_rw_sec[bmid] = line_sec
        ble_is_user[bmid] = 1
        ble_count++
        ble_order[ble_count] = bmid
    }
    if (match($0, /queued=[0-9]+/))
        ble_queued[bmid] = substr($0, RSTART+7, RLENGTH-7) + 0
}

/NEW-TXT:? / {
    # Fields after the msg_id: Hxx Sx Tx Mxx route -> text starts at bi+5
    # (same offsets with or without the colon: the tag is one token).
    for (bi = 1; bi <= NF; bi++) {
        if ($bi ~ /^x[0-9A-Fa-f]{8}$/) {
            bmid = substr($bi, 2)
            ble_newtxt_sec[bmid] = line_sec
            # Extract short text
            btxt = ""
            bcap = 0
            for (bj = bi+5; bj <= NF; bj++) {
                if ($bj ~ /^HW:/) break
                if (bcap) btxt = btxt " "
                btxt = btxt $bj
                bcap = 1
            }
            if (length(btxt) > 32) btxt = substr(btxt, 1, 32) "..."
            ble_text[bmid] = btxt
            break
        }
    }
}

/RING_TX_READ/ {
    if (match($0, /msg_id=[0-9A-Fa-f]+/)) {
        bmid = substr($0, RSTART+7, RLENGTH-7)
        if (bmid in ble_is_user) {
            ble_txr_sec[bmid] = line_sec
            if (match($0, /lat=[0-9]+/))
                ble_lat[bmid] = substr($0, RSTART+4, RLENGTH-4) + 0
        }
    }
}

/TX-LoRa:? +[0-9][0-9][0-9] / {
    # msg_id must be one of our user_msg ids (RING_WRITE src=user_msg), which
    # replaces the former "S0" test (S0 is the server flag, not "own").
    for (bi = 1; bi <= NF; bi++) {
        if ($bi ~ /^x[0-9A-Fa-f]{8}$/) {
            bmid = substr($bi, 2)
            # first TX only: a retransmission would stretch the latency
            if ((bmid in ble_is_user) && !(bmid in ble_tx_sec))
                ble_tx_sec[bmid] = line_sec
            break
        }
    }
}

END {
    # ════════════════════════════════════════════════════════
    # HEAP MONITORING
    # ════════════════════════════════════════════════════════
    print ""
    print "=== HEAP_MONITORING ==="
    if (heap_cnt > 0) {
        avg = heap_sum / heap_cnt
        sd = stddev(heap_sum, heap_sumsq, heap_cnt)
        printf "SAMPLES: %d (simple: %d, extended: %d, of which mon: %d)\n", heap_cnt, heap_cnt - heap_ext_cnt, heap_ext_cnt, heap_mon_cnt
        printf "FREE_HEAP: MIN=%d  MAX=%d  AVG=%.0f  STDDEV=%.0f\n", heap_min, heap_max, avg, sd
        if (heap_ext_cnt > 0) {
            if (heap_lwm < 999999999) printf "LOW_WATERMARK: %d (from extended monitoring)\n", heap_lwm
            if (heap_mab > 0) printf "MAX_ALLOC_BLOCK: MIN=%d  MAX=%d (largest allocatable block; MIN = worst fragmentation)\n", heap_mab_min, heap_mab
        }
        if (psrm_val != "") printf "PSRAM: %s\n", psrm_val

        print ""
        print "--- HEAP_TREND_HOURLY ---"
        printf "%-6s %10s %10s %8s\n", "HOUR", "AVG_FREE", "MIN_FREE", "SAMPLES"
        for (h = 0; h < 24; h++) {
            if (h in heap_hour_cnt) {
                printf "%02d:00  %10.0f %10d %8d\n", h, heap_hour_sum[h]/heap_hour_cnt[h], heap_hour_min[h], heap_hour_cnt[h]
            }
        }

        print ""
        print "--- STABILITY_ASSESSMENT ---"
        printf "INITIAL_FREE: %d (%s)\n", heap_first, heap_first_ts
        printf "FINAL_FREE: %d (%s)\n", heap_last, heap_last_ts
        drop = heap_first - heap_last
        drop_pct = (heap_first > 0) ? drop * 100.0 / heap_first : 0
        printf "DROP: %d (%.1f%%)\n", drop, drop_pct
        printf "MAX_MONOTONIC_DECREASE: %d consecutive samples\n", heap_mono_max

        # Steady-state analysis (skip first 30 samples = init phase)
        # Use overall stats as approximation since we track globally
        ss_var_pct = (avg > 0) ? sd * 100.0 / avg : 0
        printf "STEADY_STATE_VARIANCE: %.1f%%\n", ss_var_pct

        # Distinguish init drop from real leak:
        # If max monotonic decrease is short (< 20) AND variance is low,
        # a large drop is just boot initialization, not a leak.
        # Periodic samples = tagged (mon) plus untagged simple ones; a log
        # with only (init)/other tagged samples cannot be rated.
        if (heap_mon_cnt == 0 && heap_cnt - heap_ext_cnt == 0) {
            printf "RATING: n/a (no periodic (mon) samples)\n"
        } else if (ss_var_pct > 15) {
            printf "RATING: VOLATILE\n"
        } else if (heap_mono_max >= 50) {
            printf "RATING: MAJOR_LEAK (sustained monotonic decrease)\n"
        } else if (heap_mono_max >= 20) {
            printf "RATING: MINOR_LEAK (extended monotonic decrease: %d samples)\n", heap_mono_max
        } else if (ss_var_pct < 5 && heap_mono_max < 20) {
            printf "RATING: STABLE\n"
            if (drop_pct > 20) {
                printf "NOTE: Large initial drop (%.0f%%) is post-boot initialization, not a leak.\n", drop_pct
                printf "      Steady-state heap is stable with %.1f%% variance.\n", ss_var_pct
            }
        } else {
            printf "RATING: HEALTHY\n"
        }
    } else {
        print "  No [HEAP] data found"
    }

    # ════════════════════════════════════════════════════════
    # STARVATION EVENTS
    # ════════════════════════════════════════════════════════
    print ""
    print "=== STARVATION_EVENTS ==="
    print "--- RADIO_SILENT (gap > 20s between RX_TIMEOUT_FIRE) ---"
    if (silent_cnt > 0) {
        for (i = 1; i <= silent_cnt && i <= 20; i++)
            printf "  EVENT at %s  gap=%.1fs\n", silent_time[i], silent_gap[i]
        printf "TOTAL_SILENT_EVENTS: %d\n", silent_cnt
    } else {
        print "  NONE DETECTED"
    }

    print ""
    print "--- STUCK_STATES (non-RX_LISTEN state > 30s) ---"
    if (stuck_cnt > 0) {
        for (i = 1; i <= stuck_cnt && i <= 10; i++)
            printf "  STUCK %s at %s  duration=%.0fs\n", stuck_state[i], stuck_time[i], stuck_dur[i]
        printf "TOTAL_STUCK_EVENTS: %d\n", stuck_cnt
    } else {
        print "  NONE DETECTED"
    }

    print ""
    print "--- RING_ZOMBIES (retrying>0, ring drained [dist==0, or queued==0 on pre-BP-02 logs], 3+ consecutive) ---"
    if (zombie_cnt > 0) {
        for (i = 1; i <= zombie_cnt && i <= 10; i++)
            printf "  ZOMBIE at %s\n", zombie_time[i]
        printf "TOTAL_ZOMBIE_EVENTS: %d\n", zombie_cnt
    } else {
        print "  NONE DETECTED"
    }

    print ""
    print "--- PRIO1_STARVATION (p1 latency > 10000ms) ---"
    if (prio1_starve_cnt > 0) {
        for (i = 1; i <= prio1_starve_cnt && i <= 10; i++)
            printf "  EVENT at %s\n", prio1_time[i]
        printf "TOTAL_PRIO1_STARVATION: %d\n", prio1_starve_cnt
    } else {
        print "  NONE DETECTED"
    }

    # ════════════════════════════════════════════════════════
    # SERVER CONNECTION
    # ════════════════════════════════════════════════════════
    print ""
    print "=== SERVER_CONNECTION ==="
    printf "KEEP_HEARTBEATS: %d\n", keep_cnt
    printf "KEEP_GAPS_GT_90S: %d\n", keep_gap_cnt
    if (keep_gap_cnt > 0) {
        for (i = 1; i <= keep_gap_cnt && i <= 10; i++)
            printf "  GAP at %s  duration=%.0fs\n", keep_gap_time[i], keep_gap_dur[i]
    }
    print ""
    print "--- WIFI_EVENTS ---"
    wifi_connect = (wifi_gotip > 0) ? wifi_gotip : wifi_connect_legacy
    wifi_disconnect = (wifi_disc_event > 0) ? wifi_disc_event : wifi_disc_legacy
    printf "  WIFI_CONNECT: %d\n", wifi_connect
    printf "  WIFI_DISCONNECT: %d\n", wifi_disconnect
    printf "  WIFI_STALL: %d\n", wifi_stall
    printf "  UDP_RESET: %d\n", udp_reset
    print ""
    if (keep_cnt == 0) {
        print "SERVER_HEALTH: n/a (no [KEEP] heartbeats in log)"
    } else if (keep_gap_cnt == 0) {
        print "SERVER_HEALTH: HEALTHY (no heartbeat gaps detected)"
    } else if (keep_gap_cnt <= 2) {
        print "SERVER_HEALTH: MINOR_ISSUES (occasional gaps)"
    } else {
        print "SERVER_HEALTH: DEGRADED (frequent gaps)"
    }

    # ════════════════════════════════════════════════════════
    # NTP SYNC
    # ════════════════════════════════════════════════════════
    print ""
    print "=== NTP_SYNC ==="
    printf "NTP_SYNCS: %d\n", ntp_cnt
    if (ntp_interval_cnt > 0) {
        avg_int = ntp_interval_sum / ntp_interval_cnt
        printf "AVG_SYNC_INTERVAL: %.0fs (%.1f min)\n", avg_int, avg_int/60
    }
    if (ntp_cnt > 0) {
        printf "FIRST_SYNC: %s\n", ntp_time[1]
        printf "LAST_SYNC: %s\n", ntp_time[ntp_cnt < 200 ? ntp_cnt : 200]
    }
    if (ntp_cnt > 0 && ntp_interval_cnt > 0) {
        if (avg_int > 1200) {
            print "NTP_HEALTH: WARNING (sync interval > 20 min)"
        } else {
            print "NTP_HEALTH: HEALTHY"
        }
    } else if (ntp_cnt == 0) {
        print "NTP_HEALTH: NO_DATA"
    }

    # ════════════════════════════════════════════════════════
    # SIGNAL PROFILING
    # ════════════════════════════════════════════════════════
    print ""
    print "=== SIGNAL_PROFILING ==="
    print "Per directly-heard node (H00) - RSSI, SNR, Frequency Error"
    print ""
    printf "%-15s %5s  %8s %7s %7s  %7s %6s %6s  %9s %8s %8s\n", \
        "CALLSIGN", "PKTS", "RSSI_AVG", "RSSI_SD", "RSSI_RNG", \
        "SNR_AVG", "SNR_SD", "SNR_RNG", "FERR_AVG", "FERR_SD", "FERR_RNG"

    # Collect and sort by count
    sig_n = 0
    for (cs in sig_rssi_cnt) {
        sig_n++
        sig_list[sig_n] = cs
        sig_sort[sig_n] = sig_rssi_cnt[cs]
    }
    # Simple insertion sort by count descending
    for (i = 2; i <= sig_n; i++) {
        j = i
        while (j > 1 && sig_sort[j] > sig_sort[j-1]) {
            tmp = sig_list[j]; sig_list[j] = sig_list[j-1]; sig_list[j-1] = tmp
            tmp = sig_sort[j]; sig_sort[j] = sig_sort[j-1]; sig_sort[j-1] = tmp
            j--
        }
    }
    for (i = 1; i <= sig_n; i++) {
        cs = sig_list[i]
        n = sig_rssi_cnt[cs]
        r_avg = sig_rssi_sum[cs] / n
        r_sd = stddev(sig_rssi_sum[cs], sig_rssi_sumsq[cs], n)
        s_avg = sig_snr_sum[cs] / sig_snr_cnt[cs]
        s_sd = stddev(sig_snr_sum[cs], sig_snr_sumsq[cs], sig_snr_cnt[cs])
        if (sig_ferr_cnt[cs] > 0) {
            f_avg = sig_ferr_sum[cs] / sig_ferr_cnt[cs]
            f_sd = stddev(sig_ferr_sum[cs], sig_ferr_sumsq[cs], sig_ferr_cnt[cs])
            printf "%-15s %5d  %8.1f %7.1f %3d..%3d  %7.1f %6.1f %3d..%2d  %9.1f %8.1f %6.0f..%.0f\n", \
                cs, n, r_avg, r_sd, sig_rssi_min[cs], sig_rssi_max[cs], \
                s_avg, s_sd, sig_snr_min[cs], sig_snr_max[cs], \
                f_avg, f_sd, sig_ferr_min[cs], sig_ferr_max[cs]
        } else {
            # SETLOG-only RX (nRF52): RSSI/SNR known, no frequency error
            printf "%-15s %5d  %8.1f %7.1f %3d..%3d  %7.1f %6.1f %3d..%2d  %9s %8s %8s\n", \
                cs, n, r_avg, r_sd, sig_rssi_min[cs], sig_rssi_max[cs], \
                s_avg, s_sd, sig_snr_min[cs], sig_snr_max[cs], \
                "n/a", "n/a", "n/a"
        }
    }

    # ════════════════════════════════════════════════════════
    # FREQUENCY DRIFT
    # ════════════════════════════════════════════════════════
    print ""
    print "=== FREQUENCY_DRIFT ==="
    print "Day (06:00-17:59) vs Night (18:00-05:59) frequency error per node"
    print ""
    printf "%-15s %5s  %10s %10s %9s %8s  %s\n", \
        "CALLSIGN", "PKTS", "DAY_AVG_Hz", "NIGHT_AVG", "SHIFT_Hz", "SLOPE", "ASSESSMENT"
    for (i = 1; i <= sig_n; i++) {
        cs = sig_list[i]
        n = sig_ferr_cnt[cs]
        if (n < 5) continue
        day_avg = 0; night_avg = 0; shift = 0
        has_day = (cs in sig_ferr_day_cnt && sig_ferr_day_cnt[cs] > 0)
        has_night = (cs in sig_ferr_night_cnt && sig_ferr_night_cnt[cs] > 0)
        if (has_day) day_avg = sig_ferr_day_sum[cs] / sig_ferr_day_cnt[cs]
        if (has_night) night_avg = sig_ferr_night_sum[cs] / sig_ferr_night_cnt[cs]
        if (has_day && has_night) shift = night_avg - day_avg

        # Linear regression slope
        slope = 0
        if (n >= 2) {
            sx = sig_ferr_tx_sum[cs]
            sxx = sig_ferr_txx_sum[cs]
            sxy = sig_ferr_txy_sum[cs]
            sy = sig_ferr_sum[cs]
            denom = n * sxx - sx * sx
            if (denom != 0) slope = (n * sxy - sx * sy) / denom
        }

        assessment = "STABLE"
        if (abs(shift) > 50) assessment = "TEMP_DRIFT"
        if (abs(slope) > 20) assessment = "TRENDING"
        if (abs(shift) > 50 && abs(slope) > 20) assessment = "TEMP_DRIFT+TRENDING"

        printf "%-15s %5d  %10.1f %10.1f %9.1f %8.2f  %s\n", \
            cs, n, day_avg, \
            (has_night ? night_avg : 0), \
            shift, slope, assessment
    }
    print ""
    print "NOTE: Shifts > 50 Hz between day/night suggest crystal thermal drift."
    print "      Slope = Hz/hour trend (positive = drifting up over time)."

    # ════════════════════════════════════════════════════════
    # INTERFERER DETECTION
    # ════════════════════════════════════════════════════════
    print ""
    print "=== INTERFERER_DETECTION ==="
    # No frequency-error samples at all (nRF52, SETLOG-only): a 0 here would
    # read as "no interferer" although nothing was measured.
    ferr_total = 0
    for (cs in sig_ferr_cnt) ferr_total += sig_ferr_cnt[cs]
    if (ferr_total == 0) {
        print "PACKETS_WITH_FERR_GT_5KHZ: n/a (no frequency-error data)"
        print "NODES_AVG_FERR_GT_3KHZ: n/a (no frequency-error data)"
    } else
        printf "PACKETS_WITH_FERR_GT_5KHZ: %d\n", interf_cnt

    # Check per-node averages > 3kHz
    off_freq_cnt = 0
    for (cs in sig_ferr_cnt) {
        if (sig_ferr_cnt[cs] >= 3) {
            avg_f = abs(sig_ferr_sum[cs] / sig_ferr_cnt[cs])
            if (avg_f > 3000) {
                off_freq_cnt++
                printf "  OFF-FREQUENCY NODE: %s  avg_ferr=%.1f Hz (%d packets)\n", \
                    cs, sig_ferr_sum[cs]/sig_ferr_cnt[cs], sig_ferr_cnt[cs]
            }
        }
    }
    if (off_freq_cnt == 0 && ferr_total > 0) print "NODES_AVG_FERR_GT_3KHZ: 0"

    if (interf_cnt > 0) {
        print ""
        print "--- OFF_FREQUENCY_EVENTS (|freq_err| > 5 kHz) ---"
        printf "%-12s %-15s %10s %6s %5s\n", "TIME", "CALLSIGN", "FERR_Hz", "RSSI", "SNR"
        for (i = 1; i <= interf_cnt && i <= 20; i++) {
            printf "%-12s %-15s %10.1f %6d %5d\n", \
                interf_time[i], interf_cs[i], interf_ferr[i], interf_rssi[i], interf_snr[i]
        }
    }

    # ════════════════════════════════════════════════════════
    # TOP TALKERS
    # ════════════════════════════════════════════════════════
    print ""
    print "=== TOP_TALKERS ==="
    print "--- MOST HEARD NODES (by packet count) ---"
    # Sort heard[] by count
    tt_n = 0; tt_total = 0
    for (cs in heard) {
        tt_n++
        tt_list[tt_n] = cs
        tt_sort[tt_n] = heard[cs]
        tt_total += heard[cs]
    }
    for (i = 2; i <= tt_n; i++) {
        j = i
        while (j > 1 && tt_sort[j] > tt_sort[j-1]) {
            tmp = tt_list[j]; tt_list[j] = tt_list[j-1]; tt_list[j-1] = tmp
            tmp = tt_sort[j]; tt_sort[j] = tt_sort[j-1]; tt_sort[j-1] = tmp
            j--
        }
    }
    # Airtime: SF11/BW250 ~3ms/byte (approximate)
    printf "%-15s %6s %6s %12s\n", "CALLSIGN", "HEARD", "PCT%", "EST_AIRTIME"
    top = tt_n; if (top > 30) top = 30
    for (i = 1; i <= top; i++) {
        cs = tt_list[i]
        pct = (tt_total > 0) ? heard[cs] * 100.0 / tt_total : 0
        airtime_s = heard_size[cs] * 0.003  # 3ms per byte approx
        printf "%-15s %6d %5.1f%% %10.1fs\n", cs, heard[cs], pct, airtime_s
    }
    print ""
    print "--- OWN TX BREAKDOWN ---"
    if (tx_total > 0) {
        # S0/S1 = msg_server flag of the frame; the own-vs-relay split is not
        # derivable from this field.
        printf "TX_SERVER_FLAG_S0: %d\n", tx_srv0
        printf "TX_SERVER_FLAG_S1: %d\n", tx_srv1
        printf "TOTAL_TX: %d\n", tx_total
        printf "EST_TX_AIRTIME: %.1fs\n", tx_total_size * 0.003
    } else if (mh_synth == 1) {
        print "TOTAL_TX: n/a (no TX-LoRa lines, --info off; see SETLOG TX_SPLIT)"
    } else {
        print "TOTAL_TX: 0"
    }
    printf "TX_ACK_FRAMES (TX-Lora): %d\n", tx_ack_frames
    printf "RELAY_QUEUED: %d\n", relay_q_cnt

    # ════════════════════════════════════════════════════════
    # CHANNEL UTILIZATION DIAGRAM
    # ════════════════════════════════════════════════════════
    print ""
    print "=== CHANNEL_UTIL_DIAGRAM ==="
    print "Hourly average channel utilization (1-hour buckets)"
    print ""
    # Find max for scaling
    ch_max = 1
    for (h = 0; h < 24; h++) {
        if (h in chutil_hour_cnt) {
            v = chutil_hour_sum[h] / chutil_hour_cnt[h]
            if (v > ch_max) ch_max = v
        }
    }
    bar_width = 60
    printf "%-6s %6s  %s\n", "HOUR", "UTIL%", "BAR"
    for (h = 0; h < 24; h++) {
        if (h in chutil_hour_cnt) {
            v = chutil_hour_sum[h] / chutil_hour_cnt[h]
            # Scale to percentage (util is already %, scale bar to 100%)
            bars = int(v * bar_width / 100 + 0.5)
            if (bars < 0) bars = 0
            if (bars > bar_width) bars = bar_width
            bar_str = ""
            for (b = 0; b < bars; b++) bar_str = bar_str "#"
            for (b = bars; b < bar_width; b++) bar_str = bar_str " "
            printf "%02d:00  %5.1f%%  |%s|\n", h, v, bar_str
        }
    }

    # ════════════════════════════════════════════════════════
    # CSMA TIMING ANALYSIS
    # ════════════════════════════════════════════════════════
    print ""
    print "=== CSMA_TIMING ==="

    print "--- ADAPTIVE_WAIT_DISTRIBUTION (from RX_TIMEOUT_FIRE wait=) ---"
    printf "%-14s %7s %6s\n", "RANGE", "COUNT", "PCT%"
    if (wait_cnt > 0) {
        printf "< 3000ms       %7d %5.1f%%\n", wait_lt3, wait_lt3*100.0/wait_cnt
        printf "3000-3999ms    %7d %5.1f%%\n", wait_3_4, wait_3_4*100.0/wait_cnt
        printf "4000-4999ms    %7d %5.1f%%\n", wait_4_5, wait_4_5*100.0/wait_cnt
        printf "5000-5999ms    %7d %5.1f%%\n", wait_5_6, wait_5_6*100.0/wait_cnt
        printf "> 6000ms       %7d %5.1f%%\n", wait_gt6, wait_gt6*100.0/wait_cnt
        print ""
        printf "WAIT_SAMPLES: %d\n", wait_cnt
        printf "WAIT_AVG: %.0f ms\n", wait_sum / wait_cnt
        printf "WAIT_MIN: %d ms\n", wait_min
        printf "WAIT_MAX: %d ms\n", wait_max
    } else {
        print "  No RX_TIMEOUT_FIRE wait data"
    }

    print ""
    print "--- INTER_ARRIVAL_TIME (between consecutive Received packet:) ---"
    if (ia_cnt > 0) {
        printf "AVG: %.1fs\n", ia_sum / ia_cnt
        printf "MIN: %.1fs\n", ia_min
        printf "MAX: %.1fs\n", ia_max
        printf "SAMPLES: %d\n", ia_cnt
    } else {
        print "  No inter-arrival data"
    }

    print ""
    print "--- CAD_BUSY_VS_TRAFFIC (per hour) ---"
    printf "%-6s %8s %10s %10s %8s %10s\n", "HOUR", "TX_GATE", "CAD_BUSY_1", "CAD_BUSY_C", "CAD_FP", "CHAN_UTIL"
    for (h = 0; h < 24; h++) {
        if (h in chutil_hour_cnt || h in cad_busy1_hour || h in cad_busy_hour) {
            cu = (h in chutil_hour_cnt) ? chutil_hour_sum[h]/chutil_hour_cnt[h] : 0
            printf "%02d:00  %8d %10d %10d %8d %9.1f%%\n", h, \
                (h in tx_gate_hour ? tx_gate_hour[h] : 0), \
                (h in cad_busy1_hour ? cad_busy1_hour[h] : 0), \
                (h in cad_busy_hour ? cad_busy_hour[h] : 0), \
                (h in cad_fp_hour ? cad_fp_hour[h] : 0), cu
        }
    }

    print ""
    print "--- CSMA_ASSESSMENT ---"
    print "LoRa Parameters: SF11, BW250kHz, CR4/6, Preamble 8 symbols"
    printf "Symbol time: 8.192 ms (2^11 / 250000)\n"
    printf "Preamble duration: ~100 ms (12.25 symbols)\n"
    printf "CAD detection window: ~16 ms (2 symbols)\n"
    print ""
    if (wait_cnt > 0) {
        w_avg = wait_sum / wait_cnt
        printf "Observed adaptive wait avg: %.0f ms\n", w_avg
        printf "Observed wait range: %d - %d ms\n", wait_min, wait_max
    }
    if (cad_busy1_cnt > 0 && tx_gate_cnt > 0) {
        printf "CAD initial-busy rate: %.1f%% (%d CAD_BUSY_1 / %d scans)\n", \
            cad_busy1_cnt * 100.0 / (cad_busy1_cnt + tx_gate_cnt), cad_busy1_cnt, cad_busy1_cnt + tx_gate_cnt
    }
    if (cad_busy1_cnt > 0) {
        printf "CAD false positive rate: %.1f%% (%d / %d initial busy)\n", \
            cad_fp_cnt * 100.0 / cad_busy1_cnt, cad_fp_cnt, cad_busy1_cnt
    }
    printf "CAD confirmed busy (CAD_BUSY attempt=): %d\n", cad_busy_cnt
    printf "CAD safety timeout (forced TX, nRF52): %d\n", cad_safety_cnt
    print ""

    if (ia_cnt > 0 && wait_cnt > 0) {
        ia_avg = ia_sum / ia_cnt
        w_avg_s = (wait_sum / wait_cnt) / 1000.0
        if (ia_avg > w_avg_s * 2) {
            print "ASSESSMENT: ADEQUATE - Inter-arrival time >> wait time. Low collision risk."
        } else if (ia_avg > w_avg_s) {
            print "ASSESSMENT: MARGINAL - Inter-arrival approaching wait time. Monitor closely."
        } else {
            print "ASSESSMENT: CONGESTED - Inter-arrival < wait time. Consider longer backoff."
        }
        printf "  Inter-arrival avg: %.1fs vs wait avg: %.1fs (ratio: %.1f)\n", \
            ia_avg, w_avg_s, ia_avg / w_avg_s
    }

    # ════════════════════════════════════════════════════════
    # ACK STORM
    # ════════════════════════════════════════════════════════
    print ""
    print "=== ACK_STORM ==="
    printf "TOTAL_ACK_EVENTS: %d\n", ack_cnt
    printf "TX_ACK_FRAMES (TX-Lora): %d\n", tx_ack_frames
    print ""
    print "--- ACK_BREAKDOWN ---"
    printf "  ACK_RECEIVED: %d\n", ack_received
    printf "  ACK_SKIP_READY: %d\n", ack_skip
    printf "  ACK_REJECT: %d\n", ack_reject
    printf "  ACK_APPENDIX: %d\n", ack_appendix
    print ""
    if (ack_cnt == 0 && tx_ack_frames == 0) {
        print "ACK_STORM_VERDICT: n/a (no ACK markers in log)"
    } else if (ack_cnt < 10 && tx_ack_frames < 10) {
        print "NOTE: Very low ACK activity in this log."
    }

    # ════════════════════════════════════════════════════════
    # CAD ENHANCED STATS
    # ════════════════════════════════════════════════════════
    print ""
    print "=== CAD_ENHANCED ==="
    printf "CAD_BUSY_1: %d\n", cad_busy1_cnt
    printf "CAD_BUSY_CONFIRMED: %d\n", cad_busy_cnt
    printf "CAD_FALSE_POSITIVE: %d", cad_fp_cnt
    if (cad_busy1_cnt > 0)
        printf " (%.1f%% of CAD_BUSY_1)", cad_fp_cnt * 100.0 / cad_busy1_cnt
    printf "\n"
    printf "CAD_ABORT_BY_RX: %d\n", cad_abort_rx
    printf "CAD_SAFETY_TIMEOUT: %d\n", cad_safety_cnt

    # ════════════════════════════════════════════════════════
    # BLE TX LATENCY
    # ════════════════════════════════════════════════════════
    print ""
    print "=== BLE_TX_LATENCY ==="
    if (ble_count > 0) {
        printf "BLE user_msg events: %d\n\n", ble_count
        printf "%-10s %-14s %-35s %6s %10s %10s\n", \
            "MSG_ID", "TIME", "TEXT", "QUEUE", "Q_WAIT", "TOTAL"

        bn = 0; bt_sum = 0; bq_sum = 0
        bt_min = 999999999; bt_max = 0
        bq_min = 999999999; bq_max = 0
        for (bi = 1; bi <= ble_count; bi++) {
            bmid = ble_order[bi]
            if (!(bmid in ble_tx_sec)) continue
            bn++

            bq_wait = (bmid in ble_lat) ? ble_lat[bmid] : -1
            btotal = -1
            if (bmid in ble_newtxt_sec && bmid in ble_tx_sec) {
                btotal = (ble_tx_sec[bmid] - ble_newtxt_sec[bmid]) * 1000
                if (btotal < 0) btotal += 86400000
            } else if (bmid in ble_rw_sec && bmid in ble_tx_sec) {
                btotal = (ble_tx_sec[bmid] - ble_rw_sec[bmid]) * 1000
                if (btotal < 0) btotal += 86400000
            }

            printf "%-10s %-14s %-35s %6d %8dms %8dms\n", \
                bmid, (bmid in ble_rw_sec ? "" : "?"), \
                (bmid in ble_text ? ble_text[bmid] : "?"), \
                (bmid in ble_queued ? ble_queued[bmid] : -1), \
                bq_wait, btotal

            if (bq_wait > 0) {
                bq_sum += bq_wait; bq_cnt++
                if (bq_wait < bq_min) bq_min = bq_wait
                if (bq_wait > bq_max) bq_max = bq_wait
            }
            if (btotal >= 0) {
                bt_sum += btotal
                if (btotal < bt_min) bt_min = btotal
                if (btotal > bt_max) bt_max = btotal
                # Histogram
                if (btotal < 2000) bh_lt2++
                else if (btotal < 5000) bh_2_5++
                else if (btotal < 10000) bh_5_10++
                else if (btotal < 20000) bh_10_20++
                else bh_gt20++
                # Sorted for median
                bsorted[bn] = btotal
                # Queue depth correlation
                bqd = (bmid in ble_queued) ? ble_queued[bmid] : -1
                if (bqd >= 0 && bq_wait > 0) {
                    bqd_sum[bqd] += bq_wait
                    bqd_cnt[bqd]++
                }
            }
        }

        printf "\n--- STATISTICS (%d messages with TX) ---\n", bn
        if (bq_cnt > 0) printf "QUEUE_WAIT: AVG=%dms  MIN=%dms  MAX=%dms\n", bq_sum/bq_cnt, bq_min, bq_max
        if (bn > 0) {
            printf "END_TO_END: AVG=%dms  MIN=%dms  MAX=%dms\n", bt_sum/bn, bt_min, bt_max
            # Sort for median/p95
            for (bi = 2; bi <= bn; bi++) {
                bj = bi; bv = bsorted[bj]
                while (bj > 1 && bsorted[bj-1] > bv) {
                    bsorted[bj] = bsorted[bj-1]; bj--
                }
                bsorted[bj] = bv
            }
            bmed = int(bn/2) + 1
            bp95 = int(bn * 0.95) + 1
            if (bp95 > bn) bp95 = bn
            printf "MEDIAN:     %dms\n", bsorted[bmed]
            printf "P95:        %dms\n", bsorted[bp95]
        }

        printf "\n--- HISTOGRAM ---\n"
        printf "< 2s:   %3d  (%5.1f%%)\n", bh_lt2+0, (bn>0 ? (bh_lt2+0)*100.0/bn : 0)
        printf "2-5s:   %3d  (%5.1f%%)\n", bh_2_5+0, (bn>0 ? (bh_2_5+0)*100.0/bn : 0)
        printf "5-10s:  %3d  (%5.1f%%)\n", bh_5_10+0, (bn>0 ? (bh_5_10+0)*100.0/bn : 0)
        printf "10-20s: %3d  (%5.1f%%)\n", bh_10_20+0, (bn>0 ? (bh_10_20+0)*100.0/bn : 0)
        printf "> 20s:  %3d  (%5.1f%%)\n", bh_gt20+0, (bn>0 ? (bh_gt20+0)*100.0/bn : 0)

        printf "\n--- QUEUE_DEPTH vs LATENZ ---\n"
        printf "%-8s %10s %6s\n", "QUEUED", "AVG_WAIT", "COUNT"
        for (bq = 0; bq <= 30; bq++) {
            if (bq in bqd_cnt)
                printf "%-8d %8dms %6d\n", bq, bqd_sum[bq]/bqd_cnt[bq], bqd_cnt[bq]
        }
    } else {
        print "  No BLE user_msg events found"
    }
}
' "$LOGFILE"

# ─── CRC FORENSICS (Pass B) ───
section "CRC_FORENSICS"
awk '
BEGIN {
    # Build hex-to-decimal lookup
    split("0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15", dec, " ")
    hex_chars = "0123456789ABCDEF"
    for (i = 1; i <= 16; i++) {
        c = substr(hex_chars, i, 1)
        h2d[c] = i - 1
        h2d[tolower(c)] = i - 1
    }

    total = 0; cs0 = 0; cs1 = 0; cs2plus = 0
    noise_cnt = 0; weak_cnt = 0; collision_cnt = 0; single_cnt = 0; unclass = 0
    pending = 0
}

function hex2ascii(hexstr,    result, bytes, n, i, hi, lo, val) {
    n = split(hexstr, bytes, " ")
    result = ""
    for (i = 1; i <= n; i++) {
        if (bytes[i] !~ /^[0-9A-Fa-f][0-9A-Fa-f]$/) continue
        hi = h2d[substr(bytes[i], 1, 1)]
        lo = h2d[substr(bytes[i], 2, 1)]
        val = hi * 16 + lo
        if (val >= 32 && val <= 126)
            result = result sprintf("%c", val)
        else
            result = result "."
    }
    return result
}

# F-09: a sender is a PATH HEAD. decodeAPRS (aprs_functions.cpp) frames are
# `SRC,REL1,REL2>DEST`: the originator comes first, relays follow after a
# comma, the destination follows `>`. Path state decides, not the byte before
# a callsign: once a head is seen, every callsign up to the next `>` is a relay
# (corruption often eats the comma before a relay). A new head can only start
# outside a path. Callsign shape: 1-2 alnum + digit + 1-4 letters + -SSID.
function extract_heads(ascii_str, heads,    n, i, c, run, start, prev, k, dup, in_path) {
    n = 0
    run = ""
    start = 0
    in_path = 0
    for (i = 1; i <= length(ascii_str) + 1; i++) {
        c = (i <= length(ascii_str)) ? substr(ascii_str, i, 1) : "."
        if (c ~ /[A-Z0-9-]/) {
            if (run == "") start = i
            run = run c
        } else {
            if (run != "" && length(run) >= 4 && run ~ /^[A-Z0-9][A-Z0-9]?[0-9][A-Z][A-Z]?[A-Z]?[A-Z]?-[0-9]+$/) {
                prev = (start > 1) ? substr(ascii_str, start - 1, 1) : ""
                if (!in_path && (c == "," || c == ">") && prev != ",") {
                    dup = 0
                    for (k = 1; k <= n; k++) if (heads[k] == run) { dup = 1; break }
                    if (!dup) heads[++n] = run
                    if (c == ",") in_path = 1
                }
            }
            if (c == ">") in_path = 0
            run = ""
        }
    }
    return n
}

/CRC_ERROR/ {
    pending = 1
    crc_rssi = 0; crc_snr = 0; crc_ferr = 0; crc_size = 0; crc_ts = $2
    n_crc_lines++
    if (match($0, /rssi=-?[0-9]+/)) crc_rssi = substr($0, RSTART+5, RLENGTH-5) + 0
    if (match($0, /snr=-?[0-9]+/)) crc_snr = substr($0, RSTART+4, RLENGTH-4) + 0
    if (match($0, /freq_err=[-0-9.]+/)) crc_ferr = substr($0, RSTART+9, RLENGTH-9) + 0
    if (match($0, /size=[0-9]+/)) crc_size = substr($0, RSTART+5, RLENGTH-5) + 0
    if (crc_size == 255) n_crc_255++
    next
}

/CRC_PAYLOAD/ && pending {
    pending = 0
    total++

    # Extract hex data after the ]: marker
    hex_data = ""
    if (match($0, /\]: /)) {
        hex_data = substr($0, RSTART + 3)
    }

    ascii = hex2ascii(hex_data)

    # Extract path heads (one sender per frame, relays excluded)
    delete found_cs
    n_cs = extract_heads(ascii, found_cs)

    # Track path-head frequencies
    for (k = 1; k <= n_cs; k++) {
        crc_cs_cnt[found_cs[k]]++
    }

    # Track collision pairs
    if (n_cs >= 2) {
        # Sort first two for consistent pair key
        a = found_cs[1]; b = found_cs[2]
        if (a > b) { tmp = a; a = b; b = tmp }
        pair = a " + " b
        pair_cnt[pair]++
    }

    # Classify
    abs_ferr = crc_ferr; if (abs_ferr < 0) abs_ferr = -abs_ferr

    if (n_cs == 0) {
        cs0++
        if (abs_ferr > 3000) {
            noise_cnt++
            classify[total] = "NOISE"
        } else if (crc_snr < -15) {
            weak_cnt++
            classify[total] = "WEAK_SIGNAL"
        } else {
            noise_cnt++
            classify[total] = "NOISE"
        }
    } else if (n_cs == 1) {
        cs1++
        single_cnt++
        classify[total] = "SINGLE_CORRUPT"
    } else {
        cs2plus++
        collision_cnt++
        classify[total] = "COLLISION"
    }

    # Store RSSI/SNR/FERR distributions
    rssi_bucket = int((crc_rssi + 130) / 10)
    if (rssi_bucket < 0) rssi_bucket = 0
    if (rssi_bucket > 13) rssi_bucket = 13
    crc_rssi_dist[rssi_bucket]++

    ferr_bucket = int(abs_ferr / 200)
    if (ferr_bucket > 10) ferr_bucket = 10
    crc_ferr_dist[ferr_bucket]++
}

END {
    print "TOTAL_CRC_WITH_PAYLOAD: " total
    # ESP32 builds before f2d6532f pad every CRC dump to 255 B with stack garbage
    unrel = (n_crc_lines >= 3 && n_crc_255 == n_crc_lines)
    if (unrel) {
        print "COLLISION_ANALYSIS: unreliable (all CRC dumps size=255, ESP32 build < f2d6532f)"
        flag = " [unreliable]"
    } else {
        print "COLLISION_ANALYSIS: ok"
        flag = ""
    }
    print ""
    print "--- CALLSIGN_EXTRACTION ---"
    printf "PAYLOADS_WITH_0_CALLSIGNS: %d (%.1f%%) -- noise/weak signal (0 path heads)\n", cs0, (total>0 ? cs0*100.0/total : 0)
    printf "PAYLOADS_WITH_1_CALLSIGN:  %d (%.1f%%) -- single packet corrupted (1 path head)\n", cs1, (total>0 ? cs1*100.0/total : 0)
    printf "PAYLOADS_WITH_2+_CALLSIGNS: %d (%.1f%%) -- 2+ distinct path heads (originators; relays not counted)%s\n", cs2plus, (total>0 ? cs2plus*100.0/total : 0), flag
    print ""
    print "--- CAUSE_CLASSIFICATION ---"
    printf "COLLISION (2+ path heads):    %d%s\n", collision_cnt, flag
    printf "SINGLE_CORRUPT (1 path head): %d\n", single_cnt
    printf "WEAK_SIGNAL (snr<-15, 0 cs):  %d\n", weak_cnt
    printf "NOISE (no callsign):          %d\n", noise_cnt
    print ""

    # Top colliding callsigns
    print "--- TOP_CALLSIGNS_IN_CRC_ERRORS --- (path heads = originators only)"
    # Sort by count
    tc_n = 0
    for (cs in crc_cs_cnt) {
        tc_n++
        tc_list[tc_n] = cs
        tc_sort[tc_n] = crc_cs_cnt[cs]
    }
    for (i = 2; i <= tc_n; i++) {
        j = i
        while (j > 1 && tc_sort[j] > tc_sort[j-1]) {
            tmp = tc_list[j]; tc_list[j] = tc_list[j-1]; tc_list[j-1] = tmp
            tmp = tc_sort[j]; tc_sort[j] = tc_sort[j-1]; tc_sort[j-1] = tmp
            j--
        }
    }
    top = tc_n; if (top > 20) top = 20
    printf "%-20s %6s\n", "CALLSIGN", "COUNT"
    for (i = 1; i <= top; i++) {
        printf "%-20s %6d\n", tc_list[i], tc_sort[i]
    }

    # Top collision pairs
    print ""
    print "--- TOP_COLLISION_PAIRS ---"
    tp_n = 0
    for (p in pair_cnt) {
        tp_n++
        tp_list[tp_n] = p
        tp_sort[tp_n] = pair_cnt[p]
    }
    for (i = 2; i <= tp_n; i++) {
        j = i
        while (j > 1 && tp_sort[j] > tp_sort[j-1]) {
            tmp = tp_list[j]; tp_list[j] = tp_list[j-1]; tp_list[j-1] = tmp
            tmp = tp_sort[j]; tp_sort[j] = tp_sort[j-1]; tp_sort[j-1] = tmp
            j--
        }
    }
    top = tp_n; if (top > 15) top = 15
    printf "%-35s %6s\n", "PAIR", "COUNT"
    for (i = 1; i <= top; i++) {
        printf "%-35s %6d\n", tp_list[i], tp_sort[i]
    }

    # CRC signal distribution
    print ""
    print "--- CRC_SIGNAL_DISTRIBUTION ---"
    print "RSSI distribution of CRC errors:"
    for (i = 0; i <= 13; i++) {
        if (i in crc_rssi_dist) {
            lo = -130 + i * 10
            hi = lo + 9
            printf "  %4d..%4d dBm: %d\n", lo, hi, crc_rssi_dist[i]
        }
    }
    print ""
    print "Frequency error distribution of CRC errors:"
    for (i = 0; i <= 10; i++) {
        if (i in crc_ferr_dist) {
            lo = i * 200
            if (i == 10) {
                printf "  %4d+ Hz:     %d\n", lo, crc_ferr_dist[i]
            } else {
                printf "  %4d-%4d Hz: %d\n", lo, lo+199, crc_ferr_dist[i]
            }
        }
    }
}
' "$LOGFILE"

# ─── DONE ───
section "END"
echo "Analysis complete."

# Cleanup: temp files are removed by the EXIT trap (cleanup_normalized_tmps).
