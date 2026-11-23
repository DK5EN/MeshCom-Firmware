#!/usr/bin/env bash
# Run CBMC or ESBMC on one bounded-model-checking job (a *.check file), or on all of them.
#   run_cbmc.sh <group>/<name>.check     one job (path relative to specs/cbmc or absolute)
#   run_cbmc.sh --all                    every quick job (specs/cbmc/*/*.check with TIER=quick), results table on stdout,
#                                        writes RESULTS.md when every job produced a result
#   run_cbmc.sh --all --heavy            quick and heavy jobs
#   run_cbmc.sh --list                   job, tier, memory budget and expectation of every job, no tool is started
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
#   TIMEOUT   seconds, default 900
#   TIER      quick (default) | heavy. Heavy = known or suspected to need more than the quick budget; only with --heavy.
#   MEM       memory budget of the job in MB (default 4096 quick, 12288 heavy); above it the job is killed ("memout",
#             a tool error). Env MEM_MB overrides every job budget (CI runners with less RAM).
#   UNWIND_WAIVER  reason text. An EXPECT=pass job must keep unwinding assertions on: a loop cut at the bound without
#             an assertion makes the pass unsound. --no-unwinding-assertions in a pass job without this key is a
#             harness error.
# Tool versions are pinned in ../tools.lock (ESBMC_VERSION, CBMC_VERSION); another version is a tool error unless
# TOOL_DRIFT_OK=1.
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

LOCK="${TOOLS_LOCK:-$HERE/../tools.lock}"

jobs_all() { (cd "$HERE" && ls -1 */*.check 2>/dev/null | sort); }
job_key() { sed -n "s/^$2=[\"']\{0,1\}\([^\"']*\)[\"']\{0,1\} *\$/\1/p" "$HERE/$1" | head -1; }
job_tier() { T="$(job_key "$1" TIER)"; echo "${T:-quick}"; }

if [ "${1:-}" = "--list" ]; then
  for J in $(jobs_all); do
    T="$(job_tier "$J")"; M="$(job_key "$J" MEM)"; [ -z "$M" ] && { [ "$T" = heavy ] && M=12288 || M=4096; }
    printf '%-40s %-6s %6s MB  %s\n' "$J" "$T" "$M" "expect $(job_key "$J" EXPECT)"
  done
  exit 0
fi

# pinned tool version: "<tool> --version" must contain the version of tools.lock
tool_pinned() { # $1 tool binary, $2 lock key
  local want got
  want="$(sed -n "s/^$2=//p" "$LOCK" 2>/dev/null | head -1)"
  [ -z "$want" ] && return 0
  got="$("$1" --version 2>&1 | head -3 | tr '\n' ' ')"
  case "$got" in *"$want"*) return 0 ;; esac
  [ "${TOOL_DRIFT_OK:-0}" = 1 ] && { echo "  note: $1 is '$got', tools.lock pins $want (TOOL_DRIFT_OK=1)"; return 0; }
  echo "  TOOL ERROR: $1 is '$got', tools.lock pins $want (TOOL_DRIFT_OK=1 to run anyway)"; return 2
}

if [ "${1:-}" = "--all" ]; then
  HEAVY=0; [ "${2:-}" = "--heavy" ] && HEAVY=1
  ROWS="$(mktemp "${TMPDIR:-/tmp}/cbmc-rows.XXXXXX")"; BAD=0; TOOLERR=0; N=0; SKIPPED=0
  for J in $(jobs_all); do
    if [ "$HEAVY" = 0 ] && [ "$(job_tier "$J")" = heavy ]; then SKIPPED=$((SKIPPED + 1)); continue; fi
    N=$((N + 1))
    LOGF="$(mktemp "${TMPDIR:-/tmp}/cbmc-log.XXXXXX")"
    "$0" "$J" >"$LOGF" 2>&1; RCX=$?
    cat "$LOGF" >&2; grep '^RESULT|' "$LOGF" >>"$ROWS"; rm -f "$LOGF"
    case "$RCX" in 0) ;; 2) TOOLERR=1; echo "TOOL ERROR: $J" >&2 ;; *) BAD=1; echo "FAILED: $J (rc=$RCX)" >&2 ;; esac
  done
  TABLE="$(echo "| job | tool | tier | proves | expected | result | verdict | peak MB | time |"; echo "|---|---|---|---|---|---|---|---|---|"
           awk -F'|' '/^RESULT/ {printf "| %s | %s | %s | %s | %s | %s | %s | %s | %ss |\n", $2,$3,$8,$4,$5,$6,$9,$10,$7}' "$ROWS")"
  echo; echo "$TABLE"
  if [ "$(grep -c '^RESULT' "$ROWS")" -eq "$N" ]; then
    RF="$HERE/RESULTS.md"; [ "$HEAVY" = 0 ] && RF="$HERE/RESULTS-quick.md"
    { echo "# Bounded model checking results ($([ "$HEAVY" = 1 ] && echo "quick and heavy tier" || echo "quick tier, $SKIPPED heavy jobs not run"))"; echo
      echo "Written by \`specs/cbmc/run_cbmc.sh --all$([ "$HEAVY" = 1 ] && echo " --heavy")\`. Do not edit by hand."; echo
      echo "- Date: $(date -u +%Y-%m-%dT%H:%MZ)"
      echo "- Commit: $(git -C "$HERE" rev-parse --short HEAD 2>/dev/null || echo unknown)$(git -C "$HERE" diff --quiet HEAD -- . ../../src 2>/dev/null || echo ' plus uncommitted changes in specs/cbmc or src')"
      echo "- Tools: $("$ESBMC" --version 2>&1 | head -1), $("$CBMC" --version 2>&1 | head -1)"
      echo "- Jobs: $N, unexpected or tool error: $(awk -F'|' '$9 != "ok"' "$ROWS" | grep -c '^RESULT')"; echo
      echo "$TABLE"; } >"$RF"
    echo "results written: $RF" >&2
  else
    echo "results file not written: $(grep -c '^RESULT' "$ROWS") of $N jobs produced a result" >&2
  fi
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
TIER="quick"; MEM=""; UNWIND_WAIVER=""
# shellcheck disable=SC1090
cd "$JDIR" && . "$JOB"
# ROOT is available to .check files as $ROOT (repo root) for -I paths.

CHECKS="$(printf '%s' "$CHECKS" | tr '|' '/')"     # '|' is the RESULT field separator
case "$TIER" in quick|heavy) ;; *) echo "  $NAME: TIER must be quick or heavy (got '$TIER')"; exit 2 ;; esac
[ -z "$MEM" ] && { [ "$TIER" = heavy ] && MEM=12288 || MEM=4096; }
[ -n "${MEM_MB:-}" ] && MEM="$MEM_MB"
row() { echo "RESULT|$NAME|$TOOL|${CHECKS:-?}|$EXPECT|$1|$2|$TIER|$3|$4"; }   # result, secs, verdict, peak MB
case " $FLAGS " in *" --no-unwinding-assertions "*)
  if [ "$EXPECT" = pass ] && [ -z "$UNWIND_WAIVER" ]; then
    echo "  [$NAME] HARNESS ERROR: a pass job must keep unwinding assertions (or state UNWIND_WAIVER)"
    row "error(unwind)" 0 TOOL-ERROR 0; exit 2
  fi ;;
esac
case "$TOOL" in
  cbmc)  tool_pinned "$CBMC" CBMC_VERSION || { row "error(version)" 0 TOOL-ERROR 0; exit 2; } ;;
  esbmc) tool_pinned "$ESBMC" ESBMC_VERSION || { row "error(version)" 0 TOOL-ERROR 0; exit 2; } ;;
esac
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
mem_kb() { # physical footprint of one pid in KB; 0 if gone
  if [ -r "/proc/$1/status" ]; then      # Linux: resident + swapped
    awk '/^VmRSS:|^VmSwap:/ {s += $2} END {printf "%d\n", s}' "/proc/$1/status" 2>/dev/null; return
  fi
  # macOS: top MEM ("1856K", "3920M", "25G", optional trailing +/-)
  top -l 1 -pid "$1" -stats mem 2>/dev/null | tail -1 | awk '{
    v=$1; sub(/[+-]$/,"",v); u=substr(v,length(v)); n=substr(v,1,length(v)-1)+0
    if(u=="K")k=n; else if(u=="M")k=n*1024; else if(u=="G")k=n*1048576; else if(u=="B")k=n/1024; else k=v+0
    printf "%d\n", k }'
}
MEMOUT=0; PEAK=0; LIM=$(( MEM * 1024 ))
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
PEAKMB=$(( PEAK / 1024 ))
echo "  [$NAME] peak mem $PEAKMB MB (budget $MEM MB, tier $TIER)"
FAILED_PROPS="$(grep -E '\*\* [0-9]+ of [0-9]+ failed|FAILURE$|FAILED|Violated property' -A0 "$OUT" | head -20)"
# A callee without a body (cbmc "no body for callee", esbmc "no body for function") makes the result meaningless:
# the tool havocs or flags it. The harness must provide a model or a real implementation.
if grep -qE "no body for (callee|function)" "$OUT"; then
  echo "  [$NAME] HARNESS ERROR: callee without body:"; grep -oE "no body for (callee|function) [A-Za-z0-9_:]+" "$OUT" | sort -u | sed 's/^/    /'
  row "error(no-body)" "$SECS" TOOL-ERROR "$PEAKMB"; echo "  output kept: $OUT"; exit 2
fi
echo "  [$NAME] $TOOL rc=$RC ${SECS}s -> $GOT"

if [ "$GOT" = "error" ] || [ "$GOT" = "timeout" ] || [ "$GOT" = "memout" ]; then
  row "$GOT" "$SECS" TOOL-ERROR "$PEAKMB"
  echo "  TOOL/HARNESS ERROR ($GOT), output kept: $OUT"; tail -25 "$OUT"; exit 2
fi
if [ "$GOT" = "fail" ] && [ "$EXPECT" = "fail" ]; then
  if [ -z "$EXPECT_PROP" ]; then row "$GOT" "$SECS" TOOL-ERROR "$PEAKMB"; echo "  HARNESS ERROR: EXPECT=fail needs EXPECT_PROP"; exit 2; fi
  if ! grep -E "FAILURE|Violated property|^  [a-z]" "$OUT" | grep -qE "$EXPECT_PROP"; then
    row "fail(other property)" "$SECS" UNEXPECTED "$PEAKMB"
    echo "  VIOLATION FOUND, BUT NOT THE EXPECTED ONE ($EXPECT_PROP); output kept: $OUT"
    echo "$FAILED_PROPS" | sed 's/^/    /'; exit 1
  fi
fi
if [ "$GOT" = "$EXPECT" ]; then
  row "$GOT" "$SECS" ok "$PEAKMB"
  [ "$EXPECT" = "fail" ] && echo "  expected violation found: OK"
  [ -n "$FAILED_PROPS" ] && [ "$EXPECT" = "fail" ] && echo "$FAILED_PROPS" | sed 's/^/    /'
  rm -f "$OUT"; exit 0
fi
row "$GOT" "$SECS" UNEXPECTED "$PEAKMB"
if [ "$EXPECT" = "fail" ]; then
  echo "  EXPECTED A VIOLATION, $TOOL passed (vacuous harness?)"; rm -f "$OUT"; exit 1
fi
echo "  UNEXPECTED VIOLATION, full output kept: $OUT"
echo "$FAILED_PROPS" | sed 's/^/    /'
exit 1
