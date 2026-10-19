#!/bin/bash
# Throttle one node's web replies (TCP source port 80 -> this Mac) with pf + dummynet, to reproduce a
# slow browser (WEB-WDT-01). macOS ignores a small SO_RCVBUF, so a non-reading client alone never fills
# the node's send window. Only inbound port-80 traffic from the node is slowed; the 2323 console and
# everything else stay untouched. Runtime state only (anchor com.apple/webwdt, dummynet pipe 4711);
# nothing is written under /etc.
#
#   sudo tools/bench/web_throttle.sh on 192.168.68.71[,192.168.68.66,...] [kbit] [delay_ms]
#        kbit 0 = no bandwidth limit; delay_ms holds every reply packet (2500 = stall, 1200 = trickle).
#        A pure bandwidth limit is weak on macOS (bursts); the delay is what pins the node's send window.
#   sudo tools/bench/web_throttle.sh off
#   sudo tools/bench/web_throttle.sh status
set -euo pipefail

ANCHOR=com.apple/webwdt
PIPE=4711
TOKEN_FILE=/var/run/webwdt_pf.token

if [ "$(id -u)" -ne 0 ]; then
    echo "needs root: sudo $0 $*" >&2
    exit 2
fi

case "${1:-}" in
on)
    ips="${2:?node IP or comma-separated IPs}"
    kbit="${3:-1}"
    delay="${4:-0}"
    rules=""
    for ip in ${ips//,/ }; do
        [[ "$ip" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "bad IP: $ip" >&2; exit 2; }
        rules+="dummynet in quick proto tcp from $ip port 80 to any pipe $PIPE"$'\n'
    done
    [[ "$kbit" =~ ^[0-9]+$ ]] || { echo "bad kbit: $kbit" >&2; exit 2; }
    [[ "$delay" =~ ^[0-9]+$ ]] || { echo "bad delay: $delay" >&2; exit 2; }
    if [ "$kbit" -eq 0 ]; then bw=""; else bw="bw ${kbit}Kbit/s"; fi
    # shellcheck disable=SC2086
    dnctl pipe $PIPE config $bw delay "$delay" queue 50
    # the main ruleset carries 'dummynet-anchor "com.apple/*"'; load it in case pf was never enabled
    pfctl -q -f /etc/pf.conf 2>/dev/null || true
    printf '%s' "$rules" | pfctl -q -a $ANCHOR -f -
    [ -s $TOKEN_FILE ] || pfctl -E 2>&1 | sed -n 's/^Token : //p' > $TOKEN_FILE
    echo "throttle on: $ips port 80 -> here, ${kbit} kbit/s (0 = unlimited), delay ${delay} ms"
    ;;
off)
    pfctl -q -a $ANCHOR -F all 2>/dev/null || true
    dnctl pipe $PIPE delete 2>/dev/null || true
    if [ -s $TOKEN_FILE ]; then pfctl -q -X "$(cat $TOKEN_FILE)" 2>/dev/null || true; fi
    rm -f $TOKEN_FILE
    echo "throttle off"
    ;;
status)
    pfctl -a $ANCHOR -s dummynet 2>/dev/null || true
    dnctl pipe $PIPE show 2>/dev/null || echo "no pipe"
    ;;
*)
    sed -n '2,13p' "$0"
    exit 2
    ;;
esac
