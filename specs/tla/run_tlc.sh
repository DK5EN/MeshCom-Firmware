#!/usr/bin/env bash
# Run TLC on one cfg, or on every cfg of this directory. Formal runs are opt-in (operator 2026-11-23).
#   run_tlc.sh <config.cfg>              model check one cfg (the cfg names its spec, see markers)
#   run_tlc.sh <Spec.tla> <config.cfg>   same, spec given explicitly (archive/ models)
#   run_tlc.sh --all                     every *.cfg here; writes RESULTS.md (only when all of them ran)
#   run_tlc.sh --list                    cfg, spec and expectation of every cfg, no tool is started
#   run_tlc.sh --sany <Spec.tla>         parse check only
#   run_tlc.sh --sany-all                parse check of every *.tla here
#
# cfg markers, as comments in the first 12 lines:
#   \* tlc: spec=<Spec>.tla           required: the module this cfg belongs to
#   \* tlc: expect-fail=<Property>    a violation of exactly this property is the expected result. Any other
#                                     violation (another invariant, TypeOK, a deadlock) is an unexpected result.
#                                     A run that passes is a vacuous check and fails.
#   \* tlc: nodeadlock                pass -deadlock (terminal states exist by bound)
#
# Env: TLA_JAR (default ~/.local/share/tla/tla2tools.jar), JAVA (default: java on PATH, else the Homebrew openjdk),
#      TLC_WORKERS (default auto), TLC_HEAP (default 4g), TRACE_DIR (default traces/, gitignored),
#      TOOLS_LOCK (default ../tools.lock), TOOL_DRIFT_OK=1 accepts a jar whose checksum differs from the lock.
# Exit codes: 0 every cfg as expected, 1 an unexpected result, 2 tool, spec or harness error.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SELF="$HERE/$(basename "$0")"
JAR="${TLA_JAR:-$HOME/.local/share/tla/tla2tools.jar}"
LOCK="${TOOLS_LOCK:-$HERE/../tools.lock}"
if [ -z "${JAVA:-}" ]; then
  if [ -x /opt/homebrew/opt/openjdk/bin/java ]; then JAVA=/opt/homebrew/opt/openjdk/bin/java; else JAVA=java; fi
fi

marker() { head -12 "$1" | sed -n "s/.*tlc: $2=\([A-Za-z0-9_.]*\).*/\1/p" | head -1; }
has_marker() { head -12 "$1" | grep -q "tlc: $2"; }
# names listed under a cfg keyword (INVARIANT, INVARIANTS, PROPERTY, PROPERTIES), on the keyword line or below it
cfg_names() {
  awk -v want="$2" '
    /^[ \t]*\\\*/ { next }
    { for (i = 1; i <= NF; i++) {
        if ($i ~ /^(SPECIFICATION|CONSTANTS?|INVARIANTS?|PROPERTY|PROPERTIES|INIT|NEXT|SYMMETRY|CONSTRAINT|VIEW|CHECK_DEADLOCK)$/) {
          cur = $i; sub(/S$/, "", cur); if (cur == "PROPERTIE") cur = "PROPERTY"; continue }
        if (cur == want) print $i } }' "$1"
}

preflight() {
  if ! command -v "$JAVA" >/dev/null 2>&1; then echo "  TOOL ERROR: java not found ($JAVA)"; return 2; fi
  if [ ! -s "$JAR" ]; then echo "  TOOL ERROR: tla2tools.jar not found ($JAR)"; return 2; fi
  local want got
  want="$(sed -n 's/^TLA_JAR_SHA256=//p' "$LOCK" 2>/dev/null | head -1)"
  if [ -n "$want" ]; then
    got="$(shasum -a 256 "$JAR" | awk '{print $1}')"
    if [ "$got" != "$want" ] && [ "${TOOL_DRIFT_OK:-0}" != 1 ]; then
      echo "  TOOL ERROR: $JAR has sha256 $got, tools.lock pins $want (TOOL_DRIFT_OK=1 to run anyway)"; return 2
    fi
  fi
  return 0
}

case "${1:-}" in
  --sany)
    preflight || exit 2
    cd "$HERE" && exec "$JAVA" -cp "$JAR" tla2sany.SANY "${2:?Spec.tla}" ;;
  --sany-all)
    preflight || exit 2
    cd "$HERE" || exit 2; BAD=0
    for S in *.tla; do
      if "$JAVA" -cp "$JAR" tla2sany.SANY "$S" >/dev/null 2>&1; then echo "parse ok   $S"; else echo "PARSE FAIL $S"; BAD=2; fi
    done
    exit $BAD ;;
  --list)
    cd "$HERE" || exit 2
    for C in *.cfg; do
      E="$(marker "$C" expect-fail)"
      printf '%-44s %-16s %s\n' "$C" "$(marker "$C" spec)" "${E:+violates $E}"
    done
    exit 0 ;;
  --all)
    preflight || exit 2
    cd "$HERE" || exit 2
    ROWS="$(mktemp "${TMPDIR:-/tmp}/tlc-rows.XXXXXX")"; BAD=0; TOOLERR=0; N=0
    for C in *.cfg; do
      N=$((N + 1))
      LOGF="$(mktemp "${TMPDIR:-/tmp}/tlc-log.XXXXXX")"
      "$SELF" "$C" >"$LOGF" 2>&1; RCX=$?
      cat "$LOGF" >&2; grep '^RESULT|' "$LOGF" >>"$ROWS"; rm -f "$LOGF"
      case "$RCX" in 0) ;; 2) TOOLERR=1; echo "TOOL ERROR: $C" >&2 ;; *) BAD=1; echo "FAILED: $C (rc=$RCX)" >&2 ;; esac
    done
    TABLE="$(echo "| spec | cfg | checks | expected | result | verdict | distinct states | time |"
             echo "|---|---|---|---|---|---|---|---|"
             awk -F'|' '/^RESULT/ {printf "| %s | %s | %s | %s | %s | %s | %s | %ss |\n", $2,$3,$4,$5,$6,$7,$8,$9}' "$ROWS")"
    echo; echo "$TABLE"
    if [ "$(grep -c '^RESULT' "$ROWS")" -eq "$N" ]; then
      { echo "# TLC results"; echo
        echo "Written by \`specs/tla/run_tlc.sh --all\`. Do not edit by hand."; echo
        echo "- Date: $(date -u +%Y-%m-%dT%H:%MZ)"
        echo "- Commit: $(git -C "$HERE" rev-parse --short HEAD 2>/dev/null || echo unknown)$(git -C "$HERE" diff --quiet HEAD -- . 2>/dev/null || echo ' plus uncommitted changes in specs/tla')"
        echo "- Jar sha256: $(shasum -a 256 "$JAR" | awk '{print $1}')"
        echo "- Configurations: $N, unexpected: $(awk -F'|' '$7 != "ok"' "$ROWS" | grep -c '^RESULT')"; echo
        echo "$TABLE"; } >"$HERE/RESULTS.md"
      echo "results written: specs/tla/RESULTS.md" >&2
    else
      echo "RESULTS.md not written: $(grep -c '^RESULT' "$ROWS") of $N cfgs produced a result" >&2
    fi
    rm -f "$ROWS"
    [ "$TOOLERR" = 1 ] && exit 2
    exit $BAD ;;
esac

# ---- one cfg ----------------------------------------------------------------------------------------------
if [ $# -ge 2 ]; then SPEC="$1"; CFG="$2"; else CFG="${1:?usage: run_tlc.sh <config.cfg> | --all | --list}"; SPEC=""; fi
case "$CFG" in /*) ;; *) [ -f "$CFG" ] || CFG="$HERE/$CFG" ;; esac
[ -f "$CFG" ] || { echo "  no such cfg: $CFG"; exit 2; }
CDIR="$(cd "$(dirname "$CFG")" && pwd)"; CFG="$(basename "$CFG")"
cd "$CDIR" || exit 2
[ -n "$SPEC" ] && SPEC="$(basename "$SPEC")"
[ -z "$SPEC" ] && SPEC="$(marker "$CFG" spec)"
if [ -z "$SPEC" ] || [ ! -f "$SPEC" ]; then echo "  HARNESS ERROR: $CFG names no spec (marker tlc: spec=<Spec>.tla)"; exit 2; fi
EXPECT="$(marker "$CFG" expect-fail)"
if [ -z "$EXPECT" ] && has_marker "$CFG" expect-fail; then
  echo "  HARNESS ERROR: $CFG: expect-fail needs the property name (tlc: expect-fail=<Property>)"; exit 2
fi
INVS="$(cfg_names "$CFG" INVARIANT | tr '\n' ' ')"
PROPS="$(cfg_names "$CFG" PROPERTY | tr '\n' ' ')"
if [ -n "$EXPECT" ] && ! echo " $INVS $PROPS " | grep -q " $EXPECT "; then
  echo "  HARNESS ERROR: $CFG expects a violation of $EXPECT but does not check it"; exit 2
fi
DL=(); has_marker "$CFG" nodeadlock && DL=(-deadlock)
preflight || exit 2

# Trace compaction: one line per variable; a heap sequence of the archived heap models becomes one char per cell.
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
rm -rf "$META"
grep -E "^[0-9]+ states generated|Finished in|Model checking completed|^Error:|is violated|Temporal properties were violated|Deadlock reached" "$OUT" | sed 's/^/  /'
echo "  [$SPEC $CFG] tlc exit $RC, ${SECS}s"

# What did TLC report? TLC exit codes: 0 ok, 10 assumption, 11 deadlock, 12 safety, 13 liveness; others tool/spec error.
GOT="holds"
if [ "$RC" -ne 0 ]; then
  NAME="$(sed -n -E 's/^Error: (Invariant|Action property) ([A-Za-z0-9_]+) is violated.*/\2/p' "$OUT" | head -1)"
  if [ -n "$NAME" ]; then GOT="$NAME"
  elif grep -q "Temporal properties were violated" "$OUT"; then
    # TLC does not name the temporal property: attributable only when the cfg checks exactly one
    if [ "$(echo $PROPS | wc -w)" -eq 1 ]; then GOT="$(echo $PROPS)"; else GOT="temporal(unnamed)"; fi
  elif grep -q "Deadlock reached" "$OUT"; then GOT="deadlock"
  elif [ "$RC" -eq 10 ]; then GOT="assumption"
  else GOT="error"
  fi
fi
DIST="$(grep -E "distinct states found" "$OUT" | tail -1 | sed -E 's/.*generated, ([0-9]+) distinct.*/\1/')"
CHECKS="$(echo $INVS $PROPS | tr ' ' '\n' | grep -v '^TypeOK$' | tr '\n' ' ' | sed 's/ *$//')"
EXP="holds"; [ -n "$EXPECT" ] && EXP="violates $EXPECT"
case "$GOT" in holds) RES="holds" ;; error) RES="error" ;; *) RES="violates $GOT" ;; esac
VERDICT="UNEXPECTED"; [ "$EXP" = "$RES" ] && VERDICT="ok"; [ "$GOT" = error ] && VERDICT="TOOL-ERROR"

if [ "$GOT" != holds ] && [ "$GOT" != error ]; then
  TDIR="${TRACE_DIR:-$CDIR/traces}"; mkdir -p "$TDIR"
  awk '/^Error:/ {p=1} p' "$OUT" | grep -v '^Finished in\|^[0-9]* states generated\|^End of statistics' \
    | python3 -I -c "$COMPACT" >"$TDIR/${CFG%.cfg}.txt"
fi
echo "RESULT|$SPEC|$CFG|$CHECKS|$EXP|$RES|$VERDICT|${DIST:-?}|$SECS"

if [ "$GOT" = error ]; then echo "  TOOL/SPEC ERROR (rc=$RC), output:"; tail -30 "$OUT"; rm -f "$OUT"; exit 2; fi
rm -f "$OUT"
if [ "$VERDICT" = ok ]; then
  [ -n "$EXPECT" ] && echo "  expected violation of $EXPECT found: OK"
  exit 0
fi
if [ -n "$EXPECT" ] && [ "$GOT" = holds ]; then echo "  EXPECTED A VIOLATION OF $EXPECT, TLC passed (vacuous check?)"
elif [ -n "$EXPECT" ]; then echo "  VIOLATION FOUND, BUT NOT THE EXPECTED ONE: got $GOT, expected $EXPECT"
else echo "  UNEXPECTED VIOLATION: $GOT"
fi
exit 1
