#!/bin/bash
# End-to-end regression for the MeshCom fork, in three stages.
#
#   tools/regression.sh                 # stages 1 and 2 (no hardware)
#   tools/regression.sh --stage all     # stages 1, 2 and 3 (bench fleet on USB)
#   tools/regression.sh --stage 2       # one stage only
#   tools/regression.sh --changed       # only the steps the uncommitted changes can turn red
#   tools/regression.sh --changed <commit>   # ... plus everything committed since <commit>
#   tools/regression.sh --list          # print the plan, run nothing
#   tools/regression.sh --formal        # also TLA+ (run_tlc.sh) and ESBMC/CBMC (run_cbmc.sh), opt-in
#   tools/regression.sh --stage 3 -- --flash --extudp   # pass-through to bench_suite.py
#   python3 tools/regression_unity.py --self-test       # the Unity verdict parser
#
# Stage 1  host:   every [env:native*] in platformio.ini, in ONE `pio test` call
#                  (verdict per env from pio's JSON report, tools/regression_unity.py),
#                  then test/golden/selftest.sh (lints, tool self-tests, mock
#                  server). The topo_shadow full-window case needs four raw
#                  DK5EN-98 logs under ~/Downloads/dk5en-98-nbr/; when absent
#                  they come from the ~/meshlog/dk5en-98-nbr/ copy, else are
#                  fetched from rpizero (and mirrored into ~/meshlog).
#                  Then one `pio run` per ESP32 board env (BOARD_LINK_ENVS_ESP32: the
#                  image must build) and per nRF52 board env (BOARD_LINK_ENVS):
#                  the image must link and stay at or below
#                  BOARD_FLASH_MAX_PCT of flash.
# Stage 2  tools:  ruff lint gate (rules in ruff.toml, over tools and test,
#                  ruff pinned); pytest over
#                  tools/bench, tools/tests, tools/mock; the PEP-723 scripts
#                  in test/test_nbrlog; node --test over tools/tests/*.mjs;
#                  every jsdom page test (tools/safeboot_page_test.js and
#                  tools/webgui_*_test.js by pattern, minus LIVE_NODE_TESTS); the
#                  --self-test tools not covered by selftest.sh. Only with --formal: the TLA+
#                  models (specs/tla/run_tlc.sh --all) and the ESBMC/CBMC jobs
#                  (specs/cbmc/run_cbmc.sh --all), SKIP when the tools are missing.
#                  --formal runs the quick tier; the heavy jobs: specs/cbmc/run_cbmc.sh --all --heavy.
#                  Always: specs/selftest.sh (model lint, model drift against src/, runner verdict logic).
# Stage 3  bench:  tools/bench/bench_suite.py -- identity guard, per-board
#                  harness, OTA regression, optional flash/EXTUDP/deep-sleep
#                  on every fleet.json node that is attached over USB.
#
# Every step logs to <out>/<step>.log (default
# tools/bench/runs/regression-<timestamp>/, *.log is gitignored) and the
# Stages 1 and 2 run side by side when both are asked for (stage 2 runs no pio);
# REGRESSION_SERIAL=1 restores one after another. pytest runs in
# PYTEST_WORKERS processes (default 8). The
# script ends with one summary table (status, seconds, step, detail; skipped
# cases are part of the detail). Exit 1 if any step failed. Old run directories
# are pruned at the end: the newest REGRESSION_KEEP (default 20) stay, and so
# does every run that holds a git-tracked file or is cited under docs/.
# `pio` must never run twice at once (shared build cache): every pio step
# (Unity, the `pio project config` inside selftest.sh, board links, stage 3) sits
# in the sequential part.
#
# --changed asks tools/regression_select.py which steps the changed files can
# affect: Unity envs by include closure, the other steps by path rules. A path
# no rule knows, platformio.ini or the runner itself selects the full gate. It
# is the gate for a wave commit; the full gate stays mandatory before a release,
# an upstream PR, after an upstream sync and at the end of a campaign.
#
# The inventory behind the three stages: docs/test-suite-map.md.

set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

STAGES="1,2"
FORMAL=0
OUT=""
LIST=0
CHANGED=0
CHANGED_BASE=""
BENCH_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --stage) STAGES="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --list) LIST=1; shift ;;
        --changed) CHANGED=1; shift
            case "${1:-}" in ""|--*) ;; *) CHANGED_BASE="$1"; shift ;; esac ;;
        --formal) FORMAL=1; shift ;;
        --) shift; BENCH_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,49p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ "$STAGES" = "all" ] && STAGES="1,2,3"
want() { case ",$STAGES," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

# ---------------------------------------------------------------- change-based selection
# REG_SEL_MODE: "" (every step of the asked stages), or "select" with REG_SEL_STEPS,
# REG_SEL_ENVS, REG_SEL_SUITES, REG_SEL_LINKS and REG_SEL_PYTEST_FW. The stage 2 child
# run inherits them from the environment.
if [ "$CHANGED" = 1 ]; then
    SEL_OUT=$(python3 tools/regression_select.py --shell ${CHANGED_BASE:+--base "$CHANGED_BASE"}) || {
        echo "regression.sh: the selector failed -- run the full gate" >&2; exit 2; }
    eval "$(printf '%s\n' "$SEL_OUT" | grep -v '^#')"
    echo "== Change-based run: $CHANGED_COUNT changed file(s)${CHANGED_BASE:+ since $CHANGED_BASE}"
    printf '%s\n' "$SEL_OUT" | grep '^#' | sed 's/^# /   /' | grep -v ': no gate step reads it$' | head -60
    case "$MODE" in
        none) echo "RESULT: PASS (no gate step reads the changed files)"; exit 0 ;;
        full) echo "   -> full gate" ;;
        select)
            export REG_SEL_MODE=select REG_SEL_STEPS="$STEPS" REG_SEL_ENVS="$UNITY_ENVS"
            export REG_SEL_SUITES="$UNITY_SUITES" REG_SEL_LINKS="$NRF52_LINKS" REG_SEL_LINKS_ESP32="$ESP32_LINKS"
            export REG_SEL_PYTEST_FW="$PYTEST_FIRMWARE"
            echo "   -> steps: $STEPS"
            STAGES=""
            case " $STEPS " in *" unity "*|*" selftest "*|*" links "*) STAGES="1" ;; esac
            case " $STEPS " in *" ruff "*|*" pytest "*|*" pytest_fw "*|*" wiredoc "*|*" nbrlog "*|*" mjs "*|*" pages "*|*" selftools "*|*" specs "*|*" inventory "*)
                STAGES="${STAGES:+$STAGES,}2" ;; esac ;;
    esac
fi
# sel <step>: true when the step is part of this run.
sel() {
    [ "${REG_SEL_MODE:-}" = select ] || return 0
    case " $REG_SEL_STEPS " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

if [ -z "$OUT" ]; then
    OUT="$ROOT/tools/bench/runs/regression-$(date +%Y%m%d-%H%M%S)"
fi
[ "$LIST" = 1 ] || mkdir -p "$OUT"

# ---------------------------------------------------------------- bookkeeping
STEP_NAMES=()
STEP_STATUS=()
STEP_DETAIL=()
STEP_SECS=()
FAILS=0
# Steps run strictly one after another, so a step's duration is the time since
# the previous record() (or since the start of the run for the first one).
STEP_T0=$(date +%s)
record() {   # record <name> <OK|FAIL|SKIP> <detail>; a name is recorded once
    local i=0
    while [ $i -lt ${#STEP_NAMES[@]} ]; do
        if [ "${STEP_NAMES[$i]}" = "$1" ]; then
            echo "regression.sh: step '$1' recorded twice -- second record ignored" >&2
            return 0
        fi
        i=$((i + 1))
    done
    # REC_SECS, when set by the caller, is the step's own measured duration (steps
    # that ran inside one pio call or in the parallel stage 2 child).
    local now secs; now=$(date +%s)
    if [ -n "${REC_SECS:-}" ]; then secs=$REC_SECS; else secs=$((now - STEP_T0)); STEP_T0=$now; fi
    STEP_NAMES+=("$1"); STEP_STATUS+=("$2"); STEP_DETAIL+=("$3"); STEP_SECS+=("$secs")
    [ "$2" = "FAIL" ] && FAILS=$((FAILS + 1))
    printf '%-6s %4ss %-44s %s\n' "$2" "${STEP_SECS[$((${#STEP_SECS[@]} - 1))]}" "$1" "$3"
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
    # pytest: keep the skipped (and xfailed) counts next to "N passed" -- a skip
    # depends on local state (.pio/libdeps, a compiler) and must stay visible.
    l=$(grep -E '[0-9]+ passed' "$1" | tail -1 | grep -oE '[0-9]+ (passed|skipped|xfailed|xpassed|deselected)' | paste -sd, - | sed 's/,/, /g')
    [ -n "$l" ] && { echo "$l"; return; }
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
# The nRF52 boards have the smallest flash (815104 B) and no native env links
# their image: t_echo overflowed unnoticed for two commits (2026-10-06).
BOARD_LINK_ENVS="t_echo heltec_t114 wiscore_rak4631"
BOARD_FLASH_MAX_PCT=95
# TESTGATE-01 (operator 2026-11-23): two ESP32 boards build in the full gate, the two
# that sit on the bench. No percentage limit of our own here: the linker and PlatformIO's
# size check fail the build when the app partition overflows.
BOARD_LINK_ENVS_ESP32="heltec_wifi_lora_32_V3 ttgo_tbeam"
BOARD_FLASH_MAX_PCT_ESP32=100
board_links() {   # board_links "<envs>" <max flash percent>
    local e log rc pct BOARD_FLASH_MAX_PCT="$2"
    for e in $1; do
        if [ "$LIST" = 1 ]; then printf 'PLAN   %-44s pio run -e %s (flash <= %s %%)\n' "board link $e" "$e" "$BOARD_FLASH_MAX_PCT"; continue; fi
        log="$OUT/stage1-link-$e.log"
        pio run -e "$e" > "$log" 2>&1; rc=$?
        pct=$(grep -E '^Flash: ' "$log" | tail -1 | grep -oE '[0-9]+\.[0-9]+%' | tr -d '%')
        if [ $rc -ne 0 ] || [ -z "$pct" ]; then
            record "board link $e" FAIL "exit $rc, no image -- $log"
        elif awk -v p="$pct" -v m="$BOARD_FLASH_MAX_PCT" 'BEGIN { exit !(p > m) }'; then
            record "board link $e" FAIL "flash $pct % above $BOARD_FLASH_MAX_PCT % -- $log"
        else
            record "board link $e" OK "flash $pct %"
        fi
    done
}
stage1() {
    echo "== Stage 1: host (Unity envs + golden selftest + board builds)"
    if [ "$LIST" != 1 ] && pio_busy; then
        record "stage 1" FAIL "another pio process is running -- one pio at a time"; return 1
    fi
    local envs e args="" log json rc line st n ok fail skip secs
    envs=$(grep -oE '^\[env:native[^]]*\]' platformio.ini | tr -d '[]' | sed 's/^env://')
    if [ "${REG_SEL_MODE:-}" = select ]; then
        envs=""; sel unity && envs="$REG_SEL_ENVS"
        for e in $REG_SEL_SUITES; do args="$args -f $e"; done
    fi
    case " $envs " in *" native_topo_shadow "*) ensure_topo_raw ;; esac
    for e in $envs; do
        ENV_COUNT=$((ENV_COUNT + 1)); args="$args -e $e"
        [ "$LIST" = 1 ] && printf 'PLAN   %-44s pio test -e %s (one pio call for all envs)\n' "pio test $e" "$e"
    done
    if [ "$LIST" != 1 ] && [ -n "$envs" ]; then
        # One pio process for every native env: each separate start costs about
        # 0.6 s of project parsing (measured 2026-11-23, 8 envs: 10 s apart, 5 s together).
        # The verdict per env comes from pio's JSON report, not from the text log.
        log="$OUT/stage1-unity.log"; json="$OUT/stage1-unity.json"
        # shellcheck disable=SC2086
        pio test $args --json-output-path "$json" > "$log" 2>&1; rc=$?
        if [ ! -s "$json" ]; then
            record "pio test (all native envs)" FAIL "exit $rc, no JSON report -- $log"
        else
            while IFS="$(printf '\t')" read -r e st n ok fail skip secs; do
                TOTAL_CASES=$((TOTAL_CASES + n)); TOTAL_OK=$((TOTAL_OK + ok))
                TOTAL_FAIL=$((TOTAL_FAIL + fail)); TOTAL_SKIP=$((TOTAL_SKIP + skip))
                if [ "$st" = OK ]; then REC_SECS=$secs record "pio test $e" OK "$n cases, $ok ok, $skip skipped"
                else REC_SECS=$secs record "pio test $e" FAIL "$st, $n cases, $fail failed -- grep '$e' $log"; fi
            done <<EOF_UNITY
$(python3 tools/regression_unity.py "$json" $envs)
EOF_UNITY
            STEP_T0=$(date +%s)
        fi
    fi
    # The tools/mock suites run in the stage 2 pytest step; selftest.sh skips them then.
    # In a change-based run they belong to the pytest rule as well.
    if sel selftest; then
        if want 2 || [ "${REG_SEL_MODE:-}" = select ]; then
            SELFTEST_MOCK_IN_STAGE2=1 run_step "golden selftest" stage1-selftest sh test/golden/selftest.sh
        else run_step "golden selftest" stage1-selftest sh test/golden/selftest.sh; fi
    fi
    if sel links && [ "${REG_SEL_LINKS:-1}" = 1 ]; then board_links "$BOARD_LINK_ENVS" "$BOARD_FLASH_MAX_PCT"; fi
    if sel links && [ "${REG_SEL_LINKS_ESP32:-1}" = 1 ]; then board_links "$BOARD_LINK_ENVS_ESP32" "$BOARD_FLASH_MAX_PCT_ESP32"; fi
}

# ---------------------------------------------------------------- stage 2
# Page tests that need a live node; everything else matching the pattern runs.
LIVE_NODE_TESTS="webgui_badge_test.js webgui_tick_test.js"
# --self-test tools that test/golden/selftest.sh does not run.
SELF_TEST_TOOLS="tools/bench/tdeck_screenshot.py tools/bench/oled_screenshot.py tools/nbrlog.py tools/soakstatus.py tools/webflash.py tools/resource_watch.py tools/wire_doc_html.py tools/bench/identity_guard.py tools/bench/console_golden.py"
stage2() {
    echo "== Stage 2: host tools (pytest, nbrlog scripts, node, self-tests)"
    sel ruff && run_step "ruff lint gate" stage2-ruff \
        uv run --quiet --with ruff==0.17.0 ruff check --output-format concise \
        tools test
    if sel pytest; then
        run_step "pytest tools/bench tools/tests tools/mock tools/gps" stage2-pytest \
            uv run --quiet --with pytest --with pytest-xdist --with markdown-it-py==3.0.0 pytest -q -rs \
            -n "${PYTEST_WORKERS:-8}" tools/bench tools/tests tools/mock tools/gps
    elif sel pytest_fw; then
        # The pytest files that read firmware sources, build configuration or build patches.
        # shellcheck disable=SC2086
        run_step "pytest firmware subset" stage2-pytest \
            uv run --quiet --with pytest --with pytest-xdist --with markdown-it-py==3.0.0 pytest -q -rs \
            -n "${PYTEST_WORKERS:-8}" $REG_SEL_PYTEST_FW
    fi
    sel wiredoc && run_step "wire doc HTML up to date" stage2-wire-doc-html \
        uv run --quiet tools/wire_doc_html.py --check
    # docs/test-suite-inventory.md is generated from the tree; a new suite, env or case needs
    # `python3 tools/suite_map.py --write`.
    sel inventory && run_step "suite inventory up to date" stage2-suite-inventory \
        python3 tools/suite_map.py --check
    local f
    for f in test/test_nbrlog/test_*.py; do
        sel nbrlog || continue
        run_step "nbrlog $(basename "$f" .py)" "stage2-$(basename "$f" .py)" uv run --quiet "$f"
    done
    for f in tools/tests/*.mjs; do
        sel mjs || continue
        run_step "node $(basename "$f")" "stage2-$(basename "$f" .mjs)" \
            node --test --test-reporter=tap "$f"
    done
    local jsdom="${MESHCOM_JSDOM_DIR:-$HOME/.cache/meshcom-jsdom}"
    if sel pages && [ "$LIST" != 1 ] && [ ! -d "$jsdom/node_modules/jsdom" ]; then
        mkdir -p "$jsdom"
        npm install --silent --prefix "$jsdom" jsdom@24 > "$OUT/stage2-jsdom-install.log" 2>&1 \
            || { record "jsdom install" FAIL "page tests not run -- see $OUT/stage2-jsdom-install.log"; jsdom=""; }
    fi
    # Every page test on disk runs: a new tools/webgui_*_test.js needs no edit here.
    # LIVE_NODE_TESTS need a node on the LAN (stage 3 runs the badge test).
    for f in tools/safeboot_page_test.js tools/webgui_*_test.js; do
        case " $LIVE_NODE_TESTS " in *" $(basename "$f") "*) continue ;; esac
        sel pages || continue
        [ -n "$jsdom" ] || continue   # install failed: one FAIL above, not one per page test
        NODE_PATH="$jsdom/node_modules" run_step "node $(basename "$f")" \
            "stage2-$(basename "$f" _test.js | tr '_' '-')" node "$f"
    done
    sel selftools && run_step "self-test regression_unity.py" stage2-selftest-regression_unity \
        python3 tools/regression_unity.py --self-test
    sel selftools && run_step "self-test regression_select.py" stage2-selftest-regression_select \
        python3 tools/regression_select.py --self-test
    for f in $SELF_TEST_TOOLS; do
        sel selftools || continue
        run_step "self-test $(basename "$f")" "stage2-selftest-$(basename "$f" .py)" \
            uv run --quiet "$f" --self-test
    done
    # Formal instruments, always: static lint of the TLA+ models, drift check of the models against src/
    # (specs/manifest.json), verdict logic of both runners on canned tool output. No solver, no Java.
    sel specs && run_step "formal instruments self-test (specs/selftest.sh)" stage2-formal-selftest specs/selftest.sh
    [ "${REG_SEL_MODE:-}" = select ] && return 0
    # FORMAL-01: TLA+ models (every cfg incl. expect-fail mutations) and bounded model checking
    # (ESBMC/CBMC jobs incl. mutations). Opt-in with --formal (operator 2026-11-23: too heavy for every
    # run; run_cbmc.sh --all took > 13 min, up to 6 GB per job). Missing tools are a SKIP, not a pass.
    if [ "$FORMAL" != 1 ]; then
        record "TLA+ models" SKIP "opt-in: tools/regression.sh --formal"
        record "bounded model checking" SKIP "opt-in: tools/regression.sh --formal"
        return
    fi
    if [ -s "${TLA_JAR:-$HOME/.local/share/tla/tla2tools.jar}" ]; then
        run_step "TLA+ models (run_tlc.sh --all)" stage2-tla specs/tla/run_tlc.sh --all
    else
        record "TLA+ models" SKIP "tla2tools.jar not found (TLA_JAR)"
    fi
    if command -v esbmc >/dev/null 2>&1 && command -v cbmc >/dev/null 2>&1; then
        run_step "bounded model checking (run_cbmc.sh --all)" stage2-cbmc specs/cbmc/run_cbmc.sh --all
    else
        record "bounded model checking" SKIP "esbmc/cbmc not installed (brew install esbmc cbmc)"
    fi
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

# ---------------------------------------------------------------- housekeeping
# Keep the newest REGRESSION_KEEP run directories. A run that holds a
# git-tracked file or is named under docs/ is never removed.
prune_runs() {
    local keep="${REGRESSION_KEEP:-20}" runs="$ROOT/tools/bench/runs" d n=0 removed=0
    case "$OUT" in "$runs"/regression-*) ;; *) return 0 ;; esac
    for d in $(ls -1d "$runs"/regression-* 2>/dev/null | sort -r); do
        n=$((n + 1))
        [ $n -le "$keep" ] && continue
        [ -n "$(git ls-files "$d" 2>/dev/null | head -1)" ] && continue
        grep -rqF "$(basename "$d")" "$ROOT/docs" 2>/dev/null && continue
        rm -rf "$d" && removed=$((removed + 1))
    done
    [ $removed -gt 0 ] && echo "pruned $removed old run director$([ $removed -eq 1 ] && echo y || echo ies) (kept the newest $keep)"
    return 0
}

# ---------------------------------------------------------------- main
START=$(date +%s)
echo "regression $(git rev-parse --short HEAD 2>/dev/null) on $(git branch --show-current 2>/dev/null), stages $STAGES${REG_SEL_MODE:+ (change-based)}, out $OUT"
# Stages 1 and 2 together: stage 2 starts as a child run and works alongside
# stage 1. Stage 2 runs no pio (the one `pio project config` call of the gate
# sits in selftest.sh, stage 1), and its pytest step mostly waits on timeouts,
# so the two do not compete. The child's steps are imported into this summary.
# REGRESSION_SERIAL=1 runs them one after another, as before.
S2_PID=""
if want 1 && want 2 && [ "$LIST" != 1 ] && [ "${REGRESSION_SERIAL:-0}" != 1 ]; then
    S2_ARGS="--stage 2 --out $OUT/stage2"
    [ "$FORMAL" = 1 ] && S2_ARGS="$S2_ARGS --formal"
    # shellcheck disable=SC2086
    REGRESSION_CHILD=1 "$0" $S2_ARGS > "$OUT/stage2-child.log" 2>&1 &
    S2_PID=$!
    echo "== Stage 2 started alongside stage 1 (log $OUT/stage2-child.log)"
fi
want 1 && stage1
if [ -n "$S2_PID" ]; then
    wait "$S2_PID"; S2_RC=$?
    echo "== Stage 2: host tools (ran alongside stage 1)"
    S2_SEEN=0
    if [ -s "$OUT/stage2/summary.log" ]; then
        while IFS="$(printf '\t')" read -r st secs name detail; do
            case "$st" in OK|FAIL|SKIP) ;; *) continue ;; esac
            S2_SEEN=$((S2_SEEN + 1))
            REC_SECS=$secs record "$name" "$st" "$detail"
        done < "$OUT/stage2/summary.log"
    fi
    # The child's exit code and its step list must agree; a child that died early is a failure.
    if [ "$S2_SEEN" -eq 0 ]; then
        REC_SECS=0 record "stage 2" FAIL "child run left no summary (exit $S2_RC) -- $OUT/stage2-child.log"
    elif [ "$S2_RC" -ne 0 ] && [ "$FAILS" -eq 0 ]; then
        REC_SECS=0 record "stage 2" FAIL "child run exit $S2_RC without a failed step -- $OUT/stage2-child.log"
    fi
elif want 2; then
    stage2
fi
want 3 && stage3
[ "$LIST" = 1 ] && exit 0

echo
echo "== Summary ($(( $(date +%s) - START )) s)"
i=0
while [ $i -lt ${#STEP_NAMES[@]} ]; do
    printf '%-6s %4ss %-44s %s\n' "${STEP_STATUS[$i]}" "${STEP_SECS[$i]}" "${STEP_NAMES[$i]}" "${STEP_DETAIL[$i]}"
    i=$((i + 1))
done
if want 1; then
    echo "Unity: $ENV_COUNT envs, $TOTAL_CASES cases, $TOTAL_OK ok, $TOTAL_FAIL failed, $TOTAL_SKIP skipped"
fi
{
    echo "regression $(git rev-parse --short HEAD 2>/dev/null) stages $STAGES${REG_SEL_MODE:+ change-based: $REG_SEL_STEPS} $(date -u +%FT%TZ)"
    i=0
    while [ $i -lt ${#STEP_NAMES[@]} ]; do
        printf '%s\t%s\t%s\t%s\n' "${STEP_STATUS[$i]}" "${STEP_SECS[$i]}" "${STEP_NAMES[$i]}" "${STEP_DETAIL[$i]}"; i=$((i + 1))
    done
    want 1 && echo "unity	$ENV_COUNT envs	$TOTAL_CASES cases	$TOTAL_OK ok	$TOTAL_FAIL failed	$TOTAL_SKIP skipped"
} > "$OUT/summary.log"
echo "summary: $OUT/summary.log"
[ "${REGRESSION_CHILD:-0}" = 1 ] || prune_runs
if [ $FAILS -gt 0 ]; then echo "RESULT: FAIL ($FAILS step(s))"; exit 1; fi
if [ "${REG_SEL_MODE:-}" = select ] && [ "${REGRESSION_CHILD:-0}" != 1 ]; then
    echo "RESULT: PASS (change-based: ${#STEP_NAMES[@]} step(s); the full gate is still due before release, PR and sync)"
    exit 0
fi
echo "RESULT: PASS"
