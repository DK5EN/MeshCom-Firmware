#!/usr/bin/env bash
# Run TLC (or SANY) on one spec + cfg.
#   run_tlc.sh <Spec.tla> <config.cfg>   model check
#   run_tlc.sh --sany <Spec.tla>         parse check only
#   run_tlc.sh --all                     every cfg here (spec = heap.tla for heap*.cfg, heap_frag.tla for heap_frag*.cfg,
#                                        any other cfg names its spec with the marker "tlc: spec=<Spec>.tla"),
#                                        counterexamples of heap_frag cfgs into traces/<cfg>.txt, results table on stdout
# Env: TLA_JAR (default ~/.local/share/tla/tla2tools.jar), JAVA (default /opt/homebrew/opt/openjdk/bin/java),
#      TRACE_OUT=<file> saves the counterexample (error header + trace) of an expected-to-fail run,
#      TLC_WORKERS (default auto), TLC_HEAP (default 4g).
# The cfg may carry marker comments (first 10 lines):
#   \* tlc: expect-fail   a violation is the expected result (exit 0 when TLC reports one, 1 when it passes)
#   \* tlc: nodeadlock    pass -deadlock (terminal states are by bound: MaxJobs / ChurnMax / MaxReboots)
set -u
JAR="${TLA_JAR:-$HOME/.local/share/tla/tla2tools.jar}"
JAVA="${JAVA:-/opt/homebrew/opt/openjdk/bin/java}"
HERE="$(cd "$(dirname "$0")" && pwd)"
# TRACE_OUT is taken relative to the caller's directory (two agents wrote specs/tla/specs/tla/traces/ when it was
# resolved after the cd below); --all passes paths relative to specs/tla and runs from there.
case "${TRACE_OUT:-}" in ""|/*) ;; *) TRACE_OUT="$(pwd)/$TRACE_OUT" ;; esac
if [ "${1:-}" = "--sany" ]; then
  cd "$HERE" && exec "$JAVA" -cp "$JAR" tla2sany.SANY "$2"
fi
if [ "${1:-}" = "--all" ]; then
  cd "$HERE" || exit 2
  ROWS="$(mktemp "${TMPDIR:-/tmp}/tlc-rows.XXXXXX")"; BAD=0
  for CFGF in heap.cfg heap_restart.cfg heap_mut_*.cfg heap_frag.cfg heap_frag_*.cfg $(grep -l 'tlc: spec=' *.cfg | sort); do
    MARK="$(head -10 "$CFGF" | sed -n 's/.*tlc: spec=\([A-Za-z0-9_]*\.tla\).*/\1/p' | head -1)"
    if [ -n "$MARK" ]; then SP="$MARK"; else case "$CFGF" in heap_frag*) SP=heap_frag.tla ;; *) SP=heap.tla ;; esac; fi
    TRACE_OUT=""; case "$CFGF" in heap_frag*) case "$CFGF" in *_reach_*) ;; *) head -10 "$CFGF" | grep -q 'tlc: expect-fail' && TRACE_OUT="traces/${CFGF%.cfg}.txt" ;; esac ;; esac
    [ -n "$MARK" ] && head -10 "$CFGF" | grep -q 'tlc: expect-fail' && TRACE_OUT="traces/${CFGF%.cfg}.txt"
    export TRACE_OUT
    LOGF="$(mktemp "${TMPDIR:-/tmp}/tlc-log.XXXXXX")"
    "$HERE/$(basename "$0")" "$SP" "$CFGF" >"$LOGF" 2>&1; RCX=$?   # $0 may be relative to the caller; we cd-ed to $HERE
    cat "$LOGF" >&2; grep '^RESULT|' "$LOGF" >>"$ROWS"; rm -f "$LOGF"
    [ "$RCX" -eq 0 ] || { BAD=1; echo "FAILED: $CFGF (rc=$RCX)" >&2; }
  done
  echo; echo "| spec | cfg | checks | expected | result | distinct states | time |"; echo "|---|---|---|---|---|---|---|"
  awk -F'|' '/^RESULT/ {printf "| %s | %s | %s | %s | %s | %s | %ss |\n", $2,$3,$4,$5,$6,$7,$8}' "$ROWS"
  rm -f "$ROWS"; exit $BAD
fi
SPEC="${1:?spec.tla}"; CFG="${2:?config.cfg}"
cd "$HERE" || exit 2
EXPECT_FAIL=0; DL=()
head -10 "$CFG" | grep -q 'tlc: expect-fail' && EXPECT_FAIL=1
head -10 "$CFG" | grep -q 'tlc: nodeadlock' && DL=(-deadlock)
# Trace compaction: one line per variable; a heap sequence becomes one char per cell (legend in heap_frag.tla).
COMPACT='
import re, sys
code = {"free": ".", "stk": "S", "stg": "g", "scn": "c", "r1": "1", "r2": "2", "hs1": "h", "hs2": "H", "sha": "a",
        "lw": "l", "perm": "P", "dict": "D", "dry": "d", "cn": "n", "cm": "m", "ch": "x", "boot": "B", "p1": "b", "p2": "b"}
def cell(n):
    return code.get(n, "W" if n.startswith("w") else "t" if n.startswith("x") else "?")
out, cur = [], None
for line in sys.stdin.read().splitlines():
    if line.startswith("/\\ ") or line.startswith("State ") or not line.strip():
        if cur is not None:
            out.append(cur); cur = None
        if line.startswith("/\\ "):
            cur = line
        else:
            out.append(line)
    elif cur is not None:
        cur += " " + line.strip()
    else:
        out.append(line)
if cur is not None:
    out.append(cur)
res = []
for l in out:
    m = re.match(r"/\\ heap = << (.*) >>$", l)
    if m:
        l = "/\\ heap = " + "".join(cell(n) for n in re.findall(r"\"(\w+)\"", m.group(1)))
    res.append(l)
print("\n".join(res))
'
META="$(mktemp -d "${TMPDIR:-/tmp}/tlc.XXXXXX")"
OUT="$(mktemp "${TMPDIR:-/tmp}/tlc-out.XXXXXX")"
START=$(date +%s)
"$JAVA" -Xmx"${TLC_HEAP:-4g}" -XX:+UseParallelGC -cp "$JAR" tlc2.TLC -workers "${TLC_WORKERS:-auto}" \
  ${DL[@]+"${DL[@]}"} -metadir "$META" -config "$CFG" "$SPEC" >"$OUT" 2>&1
RC=$?
SECS=$(( $(date +%s) - START ))
grep -E "^[0-9]+ states generated|Finished in|Model checking completed|^Error:|Invariant .* is violated|Temporal properties were violated|Deadlock reached" "$OUT" | sed 's/^/  /'
echo "  [$SPEC $CFG] tlc exit $RC, ${SECS}s"
# TLC exit codes: 0 ok, 10 assumption, 11 deadlock, 12 safety (invariant), 13 liveness; >=75 tool/spec error
VIOL=0; [ "$RC" -ge 10 ] && [ "$RC" -le 13 ] && VIOL=1
if [ "$VIOL" = 1 ] && [ -n "${TRACE_OUT:-}" ]; then
  mkdir -p "$(dirname "$TRACE_OUT")"
  awk '/^Error:/ {p=1} p' "$OUT" | grep -v '^Finished in\|^[0-9]* states generated\|^End of statistics' \
    | python3 -I -c "$COMPACT" >"$TRACE_OUT"
fi
DIST="$(grep -E "distinct states found" "$OUT" | tail -1 | sed -E 's/.*generated, ([0-9]+) distinct.*/\1/')"
CHECKS="$(grep -E '^(INVARIANTS|PROPERTIES)' "$CFG" | sed -E 's/^(INVARIANTS|PROPERTIES) //; s/^TypeOK ?//' | tr '\n' ' ' | sed 's/ *$//')"
EXP="holds"; [ "$EXPECT_FAIL" = 1 ] && EXP="violated"
GOT="holds"; [ "$VIOL" = 1 ] && GOT="violated"
echo "RESULT|$SPEC|$CFG|$CHECKS|$EXP|$GOT|${DIST:-?}|$SECS"
rm -rf "$META"
if [ "$RC" -ne 0 ] && [ "$VIOL" = 0 ]; then echo "  TOOL/SPEC ERROR (rc=$RC), output:"; tail -30 "$OUT"; rm -f "$OUT"; exit 2; fi
rm -f "$OUT"
if [ "$EXPECT_FAIL" = 1 ]; then
  [ "$VIOL" = 1 ] && { echo "  expected violation found: OK"; exit 0; }
  echo "  EXPECTED A VIOLATION, TLC passed (vacuous check?)"; exit 1
fi
[ "$VIOL" = 1 ] && { echo "  UNEXPECTED VIOLATION"; exit 1; }
exit 0
