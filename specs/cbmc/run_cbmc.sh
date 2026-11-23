#!/usr/bin/env bash
# Run CBMC or ESBMC on one bounded-model-checking job (a *.check file), or on all of them.
#   run_cbmc.sh <group>/<name>.check     one job (path relative to specs/cbmc or absolute)
#   run_cbmc.sh --all                    every specs/cbmc/*/*.check, results table on stdout
#   run_cbmc.sh --list                   list the jobs
#
# A .check file is a shell fragment, sourced in its own directory. Keys:
#   TOOL      cbmc | esbmc
#             Use esbmc for C++ translation units. CBMC 6.11's C++ front end evaluates sizeof(ref) as the
#             pointer size (memset(&out, 0, sizeof(out)) on a reference parameter clears 4 bytes): false alarms.
#             cbmc only for plain C-style code without references. Harness nondet: nondet_int()/nondet_uchar()
#             declared extern "C" with no body, constraints with __ESBMC_assume / __CPROVER_assume.
#   SOURCES   harness and firmware sources, relative to the .check directory (space separated)
#   CFLAGS    -I / -D flags handed to the front end (both tools take -I/-D directly)
#   FLAGS     extra tool flags (e.g. "--unwind 257 --unwindset ..."; --function if not main)
#   EXPECT    pass | fail   (fail = mutation or reachability witness: the tool MUST report a violation,
#                            otherwise the job is vacuous and the run fails)
#   CHECKS    short text for the results table (what the job proves)
#   EXPECT_PROP  (EXPECT=fail only, required) extended regex one FAILURE line must match, e.g.
#             'main.assertion|array.*upper bound'. A mutation that turns red for an unrelated reason
#             (a missing library model, a different property) is not evidence and counts as unexpected.
#   TIMEOUT   seconds, default 900; env MEM_MB caps physical footprint per job (default 6144, "memout")
#
# Fixed flags (the reason this wrapper exists, keep them in one place):
#   --32                                  ILP32 like Xtensa / Cortex-M; the host default LP64 changes long/pointer widths
#   cbmc:  --bounds-check --pointer-check --pointer-overflow-check --signed-overflow-check
#          --undefined-shift-check --div-by-zero-check --unwinding-assertions
#   esbmc: --overflow-check  (bounds, pointer, div-by-zero and unwinding assertions are on by default)
# A job may add --unsigned-overflow-check or --conversion-check in FLAGS where wrap-around is a defect.
#
# Exit codes: 0 every job as expected, 1 an unexpected result, 2 tool / harness error.
# Unexpected failures leave the full tool output in $TMPDIR and print its path plus the tail.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CBMC="${CBMC:-cbmc}"
ESBMC="${ESBMC:-esbmc}"

jobs_all() { (cd "$HERE" && ls -1 */*.check 2>/dev/null | sort); }

if [ "${1:-}" = "--list" ]; then jobs_all; exit 0; fi

if [ "${1:-}" = "--all" ]; then
  ROWS="$(mktemp "${TMPDIR:-/tmp}/cbmc-rows.XXXXXX")"; BAD=0; TOOLERR=0
  for J in $(jobs_all); do
    LOGF="$(mktemp "${TMPDIR:-/tmp}/cbmc-log.XXXXXX")"
    "$0" "$J" >"$LOGF" 2>&1; RCX=$?
    cat "$LOGF" >&2; grep '^RESULT|' "$LOGF" >>"$ROWS"; rm -f "$LOGF"
    case "$RCX" in 0) ;; 2) TOOLERR=1; echo "TOOL ERROR: $J" >&2 ;; *) BAD=1; echo "FAILED: $J (rc=$RCX)" >&2 ;; esac
  done
  echo; echo "| job | tool | proves | expected | result | time |"; echo "|---|---|---|---|---|---|"
  awk -F'|' '/^RESULT/ {printf "| %s | %s | %s | %s | %s | %ss |\n", $2,$3,$4,$5,$6,$7}' "$ROWS"
  rm -f "$ROWS"
  [ "$TOOLERR" = 1 ] && exit 2
  exit $BAD
fi

JOB="${1:?usage: run_cbmc.sh <group>/<name>.check | --all | --list}"
case "$JOB" in /*) ;; *) JOB="$HERE/$JOB" ;; esac
[ -f "$JOB" ] || { echo "no such job: $JOB"; exit 2; }
JDIR="$(cd "$(dirname "$JOB")" && pwd)"
NAME="$(basename "$JDIR")/$(basename "$JOB" .check)"

TOOL=""; SOURCES=""; CFLAGS=""; FLAGS=""; EXPECT="pass"; EXPECT_PROP=""; CHECKS=""; TIMEOUT=900
# shellcheck disable=SC1090
cd "$JDIR" && . "$JOB"
# ROOT is available to .check files as $ROOT (repo root) for -I paths.

case "$TOOL" in
  cbmc)  CMD=("$CBMC" --32 --bounds-check --pointer-check --pointer-overflow-check --signed-overflow-check
               --undefined-shift-check --div-by-zero-check --unwinding-assertions) ;;
  esbmc) CMD=("$ESBMC" --32 --overflow-check) ;;
  *) echo "  $NAME: TOOL must be cbmc or esbmc (got '$TOOL')"; exit 2 ;;
esac
# shellcheck disable=SC2206
CMD+=($CFLAGS $FLAGS $SOURCES)

OUT="$(mktemp "${TMPDIR:-/tmp}/cbmc-out.XXXXXX")"
START=$(date +%s)
perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$TIMEOUT" "${CMD[@]}" >"$OUT" 2>&1 &
TPID=$!
# Memory cap (macOS has no working ulimit -v / RLIMIT_AS): solver blow-ups took the 24 GB machine down twice
# (2026-11-23). The cap reads the physical footprint from top's MEM column (includes compressed and swapped
# pages; ps rss does not and showed < 6 GB while the job held 25 GB). Over MEM_MB (default 6144) the job is
# killed and reported as "memout", a tool error.
mem_kb() { # top MEM of one pid in KB ("1856K", "3920M", "25G", optional trailing +/-); 0 if gone
  top -l 1 -pid "$1" -stats mem 2>/dev/null | tail -1 | awk '{
    v=$1; sub(/[+-]$/,"",v); u=substr(v,length(v)); n=substr(v,1,length(v)-1)+0
    if(u=="K")k=n; else if(u=="M")k=n*1024; else if(u=="G")k=n*1048576; else if(u=="B")k=n/1024; else k=v+0
    printf "%d\n", k }'
}
MEMOUT=0; PEAK=0; LIM=$(( ${MEM_MB:-6144} * 1024 ))
while kill -0 "$TPID" 2>/dev/null; do
  RSS=$(mem_kb "$TPID"); RSS=${RSS:-0}
  [ "$RSS" -gt "$PEAK" ] && PEAK=$RSS
  if [ "$RSS" -gt "$LIM" ]; then MEMOUT=1; kill -9 "$TPID" 2>/dev/null; break; fi
  sleep 0.5
done
wait "$TPID"; RC=$?
SECS=$(( $(date +%s) - START ))

GOT="error"
if [ "$MEMOUT" = 1 ]; then GOT="memout"
elif grep -q "VERIFICATION SUCCESSFUL" "$OUT"; then GOT="pass"
elif grep -q "VERIFICATION FAILED" "$OUT"; then GOT="fail"
elif [ "$RC" -eq 142 ]; then GOT="timeout"
fi
echo "  [$NAME] peak mem $(( PEAK / 1024 )) MB"
FAILED_PROPS="$(grep -E '\*\* [0-9]+ of [0-9]+ failed|FAILURE$|FAILED|Violated property' -A0 "$OUT" | head -20)"
# A callee without a body (cbmc "no body for callee", esbmc "no body for function") makes the result meaningless:
# the tool havocs or flags it. The harness must provide a model or a real implementation.
if grep -qE "no body for (callee|function)" "$OUT"; then
  echo "  [$NAME] HARNESS ERROR: callee without body:"; grep -oE "no body for (callee|function) [A-Za-z0-9_:]+" "$OUT" | sort -u | sed 's/^/    /'
  echo "RESULT|$NAME|$TOOL|${CHECKS:-?}|$EXPECT|error(no-body)|$(( $(date +%s) - START ))"; echo "  output kept: $OUT"; exit 2
fi
echo "  [$NAME] $TOOL rc=$RC ${SECS}s -> $GOT"
echo "RESULT|$NAME|$TOOL|${CHECKS:-?}|$EXPECT|$GOT|$SECS"

if [ "$GOT" = "error" ] || [ "$GOT" = "timeout" ] || [ "$GOT" = "memout" ]; then
  echo "  TOOL/HARNESS ERROR ($GOT), output kept: $OUT"; tail -25 "$OUT"; exit 2
fi
if [ "$GOT" = "fail" ] && [ "$EXPECT" = "fail" ]; then
  if [ -z "$EXPECT_PROP" ]; then echo "  HARNESS ERROR: EXPECT=fail needs EXPECT_PROP"; exit 2; fi
  if ! grep -E "FAILURE|Violated property|^  [a-z]" "$OUT" | grep -qE "$EXPECT_PROP"; then
    echo "  VIOLATION FOUND, BUT NOT THE EXPECTED ONE ($EXPECT_PROP); output kept: $OUT"
    echo "$FAILED_PROPS" | sed 's/^/    /'; exit 1
  fi
fi
if [ "$GOT" = "$EXPECT" ]; then
  [ "$EXPECT" = "fail" ] && echo "  expected violation found: OK"
  [ -n "$FAILED_PROPS" ] && [ "$EXPECT" = "fail" ] && echo "$FAILED_PROPS" | sed 's/^/    /'
  rm -f "$OUT"; exit 0
fi
if [ "$EXPECT" = "fail" ]; then
  echo "  EXPECTED A VIOLATION, $TOOL passed (vacuous harness?)"; rm -f "$OUT"; exit 1
fi
echo "  UNEXPECTED VIOLATION, full output kept: $OUT"
echo "$FAILED_PROPS" | sed 's/^/    /'
exit 1
