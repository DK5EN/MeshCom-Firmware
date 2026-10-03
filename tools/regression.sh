#!/bin/bash
# End-to-end regression for the MeshCom fork, in three stages.
#
#   tools/regression.sh                 # stages 1 and 2 (no hardware)
#   tools/regression.sh --stage all     # stages 1, 2 and 3 (bench fleet on USB)
#   tools/regression.sh --stage 2       # one stage only
#   tools/regression.sh --list          # print the plan, run nothing
#   tools/regression.sh --stage 3 -- --flash --extudp   # pass-through to bench_suite.py
#
# Stage 1  host:   every [env:native*] in platformio.ini, one `pio test` each,
#                  then test/golden/selftest.sh (lints, tool self-tests, mock
#                  server). The topo_shadow full-window case needs four raw
#                  DK5EN-98 logs under ~/Downloads/dk5en-98-nbr/; when absent
#                  they come from the ~/meshlog/dk5en-98-nbr/ copy, else are
#                  fetched from rpizero (and mirrored into ~/meshlog).
# Stage 2  tools:  ruff syntax gate (E9/F63/F7/F82 over tools, test/golden,
#                  test/test_nbrlog; config in ruff.toml); pytest over
#                  tools/bench, tools/tests, tools/mock; the PEP-723 scripts
#                  in test/test_nbrlog; node --test over tools/tests/*.mjs;
#                  the jsdom-based safeboot page test; the four --self-test
#                  tools not covered by selftest.sh.
# Stage 3  bench:  tools/bench/bench_suite.py -- identity guard, per-board
#                  harness, OTA regression, optional flash/EXTUDP/deep-sleep
#                  on every fleet.json node that is attached over USB.
#
# Every step logs to <out>/<step>.log (default
# tools/bench/runs/regression-<timestamp>/, *.log is gitignored) and the
# script ends with one summary table. Exit 1 if any step failed. Stages run
# sequentially: `pio` must never run twice at once (shared build cache), and
# selftest.sh itself shells out to `pio project config`.
#
# The inventory behind the three stages: docs/test-suite-map.md.

set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

STAGES="1,2"
OUT=""
LIST=0
BENCH_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --stage) STAGES="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --list) LIST=1; shift ;;
        --) shift; BENCH_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ "$STAGES" = "all" ] && STAGES="1,2,3"
want() { case ",$STAGES," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

if [ -z "$OUT" ]; then
    OUT="$ROOT/tools/bench/runs/regression-$(date +%Y%m%d-%H%M%S)"
fi
[ "$LIST" = 1 ] || mkdir -p "$OUT"

# ---------------------------------------------------------------- bookkeeping
STEP_NAMES=()
STEP_STATUS=()
STEP_DETAIL=()
FAILS=0
record() {   # record <name> <OK|FAIL|SKIP> <detail>; a name is recorded once
    local i=0
    while [ $i -lt ${#STEP_NAMES[@]} ]; do
        if [ "${STEP_NAMES[$i]}" = "$1" ]; then
            echo "regression.sh: step '$1' recorded twice -- second record ignored" >&2
            return 0
        fi
        i=$((i + 1))
    done
    STEP_NAMES+=("$1"); STEP_STATUS+=("$2"); STEP_DETAIL+=("$3")
    [ "$2" = "FAIL" ] && FAILS=$((FAILS + 1))
    printf '%-6s %-44s %s\n' "$2" "$1" "$3"
}
# run_step <name> <log-basename> <command...>: runs, logs, records by exit code.
run_step() {
    local name="$1" log="$OUT/$2.log"; shift 2
    if [ "$LIST" = 1 ]; then printf 'PLAN   %-44s %s\n' "$name" "$*"; return 0; fi
    "$@" > "$log" 2>&1
    local rc=$?
    if [ $rc -eq 0 ]; then record "$name" OK "$(step_detail "$log")"
    else record "$name" FAIL "exit $rc, see $log"; fi
    return $rc
}
# step_detail <log>: one-line count pulled from a log. The tools that count
# themselves win: selftest.sh's closing "selftest: N commands run" line, then
# the test_nbrlog scripts' "nbrlog: <name>: M checks" line; only after those
# the pytest / node TAP / PASS-line heuristics.
step_detail() {
    local l
    l=$(grep -oE '^selftest: [0-9]+ commands' "$1" | tail -1)
    [ -n "$l" ] && { echo "${l#selftest: }"; return; }
    l=$(grep -oE '^nbrlog: [^:]+: [0-9]+ checks' "$1" | tail -1)
    [ -n "$l" ] && { echo "${l##*: }"; return; }
    grep -q '^All checks passed!' "$1" && { echo "0 errors"; return; }
    l=$(grep -oE '^Found [0-9]+ errors?' "$1" | tail -1); [ -n "$l" ] && { echo "${l#Found }"; return; }
    l=$(grep -oE '[0-9]+ passed' "$1" | tail -1); [ -n "$l" ] && { echo "$l"; return; }
    l=$(grep -oE '^# pass [0-9]+' "$1" | tail -1); [ -n "$l" ] && { echo "${l#\# }"; return; }
    l=$(grep -cE '^(PASS|ok|OK)\b' "$1"); [ "$l" -gt 0 ] && { echo "$l checks"; return; }
    echo "ok"
}

pio_busy() {
    pgrep -f 'pio (test|run|project)' >/dev/null 2>&1
}

# ---------------------------------------------------------------- stage 1
TOTAL_CASES=0; TOTAL_OK=0; TOTAL_FAIL=0; TOTAL_SKIP=0; ENV_COUNT=0
# The raw window is looked up in this order: ~/Downloads/dk5en-98-nbr (the only
# path test_topo_shadow reads), then the second copy in ~/meshlog/dk5en-98-nbr
# (copied into ~/Downloads), then rpizero (and a successful fetch is mirrored
# into ~/meshlog so the second copy exists afterwards).
raw_window_complete() {   # raw_window_complete <dir>: all four day logs present and non-empty
    local d
    for d in 21 22 23 24; do [ -s "$1/2026-09-$d.log" ] || return 1; done
}
ensure_topo_raw() {
    local dir="$HOME/Downloads/dk5en-98-nbr" bak="$HOME/meshlog/dk5en-98-nbr" d
    if raw_window_complete "$dir"; then record "topo_shadow raw window" OK "$dir"; return; fi
    [ "$LIST" = 1 ] && { echo "PLAN   topo_shadow raw window: ~/Downloads, else ~/meshlog copy, else fetch from rpizero"; return; }
    if raw_window_complete "$bak"; then
        mkdir -p "$dir"
        for d in 21 22 23 24; do cp -p "$bak/2026-09-$d.log" "$dir/"; done
        record "topo_shadow raw window" OK "from ~/meshlog ($bak -> $dir)"
        return
    fi
    mkdir -p "$dir"
    if scp -q -o BatchMode=yes -o ConnectTimeout=5 \
        'rpizero.local:~/meshlog/dk5en-98/2026-09-2[1-4].log' "$dir/" 2>/dev/null; then
        mkdir -p "$bak"
        for d in 21 22 23 24; do [ -s "$dir/2026-09-$d.log" ] && cp -p "$dir/2026-09-$d.log" "$bak/"; done
        record "topo_shadow raw window" OK "fetched from rpizero into $dir (copy in $bak)"
    else
        record "topo_shadow raw window" SKIP "not local, rpizero unreachable -- full-window case will be IGNOREd"
    fi
}
stage1() {
    echo "== Stage 1: host (Unity envs + golden selftest)"
    if [ "$LIST" != 1 ] && pio_busy; then
        record "stage 1" FAIL "another pio process is running -- one pio at a time"; return 1
    fi
    ensure_topo_raw
    local envs e log line n ok fail skip rc
    envs=$(grep -oE '^\[env:native[^]]*\]' platformio.ini | tr -d '[]' | sed 's/^env://')
    for e in $envs; do
        ENV_COUNT=$((ENV_COUNT + 1))
        if [ "$LIST" = 1 ]; then printf 'PLAN   %-44s pio test -e %s\n' "pio test $e" "$e"; continue; fi
        log="$OUT/stage1-$e.log"
        pio test -e "$e" > "$log" 2>&1; rc=$?
        line=$(grep -E '[0-9]+ test cases?:' "$log" | tail -1)
        n=$(echo "$line" | grep -oE '[0-9]+ test case' | grep -oE '[0-9]+'); n=${n:-0}
        ok=$(echo "$line" | grep -oE '[0-9]+ succeeded' | grep -oE '[0-9]+'); ok=${ok:-0}
        fail=$(echo "$line" | grep -oE '[0-9]+ failed' | grep -oE '[0-9]+'); fail=${fail:-0}
        skip=$(echo "$line" | grep -oE '[0-9]+ skipped' | grep -oE '[0-9]+'); skip=${skip:-0}
        TOTAL_CASES=$((TOTAL_CASES + n)); TOTAL_OK=$((TOTAL_OK + ok))
        TOTAL_FAIL=$((TOTAL_FAIL + fail)); TOTAL_SKIP=$((TOTAL_SKIP + skip))
        if [ $rc -eq 0 ] && [ "$fail" = 0 ] && [ "$n" != 0 ]; then
            record "pio test $e" OK "$n cases, $ok ok, $skip skipped"
        else
            record "pio test $e" FAIL "exit $rc, $n cases, $fail failed -- $log"
        fi
    done
    run_step "golden selftest" stage1-selftest sh test/golden/selftest.sh
}

# ---------------------------------------------------------------- stage 2
stage2() {
    echo "== Stage 2: host tools (pytest, nbrlog scripts, node, self-tests)"
    run_step "ruff syntax gate" stage2-ruff \
        uv run --quiet --with ruff ruff check --select E9,F63,F7,F82 --output-format concise \
        tools test/golden test/test_nbrlog
    run_step "pytest tools/bench tools/tests tools/mock" stage2-pytest \
        uv run --quiet --with pytest pytest -q tools/bench tools/tests tools/mock
    local f
    for f in test/test_nbrlog/test_*.py; do
        run_step "nbrlog $(basename "$f" .py)" "stage2-$(basename "$f" .py)" uv run --quiet "$f"
    done
    for f in tools/tests/*.mjs; do
        run_step "node $(basename "$f")" "stage2-$(basename "$f" .mjs)" \
            node --test --test-reporter=tap "$f"
    done
    local jsdom="${MESHCOM_JSDOM_DIR:-$HOME/.cache/meshcom-jsdom}"
    if [ "$LIST" != 1 ] && [ ! -d "$jsdom/node_modules/jsdom" ]; then
        mkdir -p "$jsdom"
        npm install --silent --prefix "$jsdom" jsdom@24 > "$OUT/stage2-jsdom-install.log" 2>&1 \
            || record "jsdom install" FAIL "see $OUT/stage2-jsdom-install.log"
    fi
    NODE_PATH="$jsdom/node_modules" run_step "node safeboot_page_test.js" stage2-safeboot-page \
        node tools/safeboot_page_test.js
    for f in tools/nbrlog.py tools/soakstatus.py tools/webflash.py tools/resource_watch.py; do
        run_step "self-test $(basename "$f")" "stage2-selftest-$(basename "$f" .py)" \
            uv run --quiet "$f" --self-test
    done
}

# ---------------------------------------------------------------- stage 3
stage3() {
    echo "== Stage 3: bench fleet (USB)"
    if [ "$LIST" = 1 ]; then
        python3 tools/bench/bench_suite.py --dry-run ${BENCH_ARGS[@]+"${BENCH_ARGS[@]}"}
        return
    fi
    if pio_busy; then
        record "stage 3" FAIL "another pio process is running -- one pio at a time"; return 1
    fi
    python3 tools/bench/bench_suite.py --out "$OUT" ${BENCH_ARGS[@]+"${BENCH_ARGS[@]}"} 2>&1 | tee "$OUT/stage3-bench.log"
    local rc=${PIPESTATUS[0]}
    if [ -f "$OUT/bench-summary.json" ]; then
        local line
        line=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d['summary'])" "$OUT/bench-summary.json")
        if [ "$rc" -eq 0 ]; then record "bench suite" OK "$line"; else record "bench suite" FAIL "$line"; fi
    else
        record "bench suite" FAIL "exit $rc, no bench-summary.json -- $OUT/stage3-bench.log"
    fi
}

# ---------------------------------------------------------------- main
START=$(date +%s)
echo "regression $(git rev-parse --short HEAD 2>/dev/null) on $(git branch --show-current 2>/dev/null), stages $STAGES, out $OUT"
want 1 && stage1
want 2 && stage2
want 3 && stage3
[ "$LIST" = 1 ] && exit 0

echo
echo "== Summary ($(( $(date +%s) - START )) s)"
i=0
while [ $i -lt ${#STEP_NAMES[@]} ]; do
    printf '%-6s %-44s %s\n' "${STEP_STATUS[$i]}" "${STEP_NAMES[$i]}" "${STEP_DETAIL[$i]}"
    i=$((i + 1))
done
if want 1; then
    echo "Unity: $ENV_COUNT envs, $TOTAL_CASES cases, $TOTAL_OK ok, $TOTAL_FAIL failed, $TOTAL_SKIP skipped"
fi
{
    echo "regression $(git rev-parse --short HEAD 2>/dev/null) stages $STAGES $(date -u +%FT%TZ)"
    i=0
    while [ $i -lt ${#STEP_NAMES[@]} ]; do
        printf '%s\t%s\t%s\n' "${STEP_STATUS[$i]}" "${STEP_NAMES[$i]}" "${STEP_DETAIL[$i]}"; i=$((i + 1))
    done
    want 1 && echo "unity	$ENV_COUNT envs	$TOTAL_CASES cases	$TOTAL_OK ok	$TOTAL_FAIL failed	$TOTAL_SKIP skipped"
} > "$OUT/summary.log"
echo "summary: $OUT/summary.log"
if [ $FAILS -gt 0 ]; then echo "RESULT: FAIL ($FAILS step(s))"; exit 1; fi
echo "RESULT: PASS"
