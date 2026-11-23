#!/usr/bin/env bash
# Self-test of the formal-check instruments. Starts NO solver and no Java: the runners are driven with
# fake tools that print canned output. Part of the host gate (tools/regression.sh stage 2).
#   1. tla_lint.py     static lint of every model and cfg
#   2. drift.py        its own self-test, then the drift check against src/
#   3. run_tlc.sh      verdict logic on canned TLC output
#   4. run_cbmc.sh     verdict logic on canned ESBMC output
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$(mktemp -d "${TMPDIR:-/tmp}/formal-selftest.XXXXXX")"
trap 'rm -rf "$W"' EXIT
FAILS=0; N=0
ok()  { N=$((N + 1)); }
bad() { N=$((N + 1)); FAILS=$((FAILS + 1)); echo "FAIL: $*"; }
expect_rc() { # name, wanted rc, actual rc
  if [ "$2" -eq "$3" ]; then ok; else bad "$1: exit $3, expected $2"; fi
}

python3 -I "$HERE/tla_lint.py" "$HERE/tla" >"$W/lint.out" 2>&1; expect_rc "tla_lint" 0 $?
[ -s "$W/lint.out" ] && tail -1 "$W/lint.out"
python3 -I "$HERE/drift.py" --self-test >"$W/d1.out" 2>&1; expect_rc "drift self-test" 0 $?
python3 -I "$HERE/drift.py" --check >"$W/d2.out" 2>&1; RC=$?; expect_rc "drift check" 0 $RC
[ "$RC" -ne 0 ] && cat "$W/d2.out"
tail -1 "$W/d2.out"

# ---- run_tlc.sh with a fake java ------------------------------------------------------------------------
cat >"$W/java" <<'FAKE'
#!/usr/bin/env bash
[ -n "${FAKE_OUT:-}" ] && printf '%b\n' "$FAKE_OUT"
echo "100 states generated, 42 distinct states found, 0 states left on queue."
exit "${FAKE_RC:-0}"
FAKE
chmod +x "$W/java"; : >"$W/tla2tools.jar"; echo x >"$W/tla2tools.jar"; : >"$W/empty.lock"
mkdir -p "$W/t"; echo "---- MODULE m ----" >"$W/t/m.tla"
mkcfg() { # name, marker lines..., then INVARIANT/PROPERTY text as last arg
  local f="$W/t/$1.cfg"; shift
  { echo '\* tlc: spec=m.tla'; while [ $# -gt 1 ]; do echo "\\* tlc: $1"; shift; done
    echo "SPECIFICATION Spec"; printf '%b\n' "$1"; } >"$f"
}
tlc() { # cfg, rc, output  -> runner exit code
  FAKE_RC="$2" FAKE_OUT="$3" JAVA="$W/java" TLA_JAR="$W/tla2tools.jar" TOOLS_LOCK="$W/empty.lock" TRACE_DIR="$W/traces" \
    "$HERE/tla/run_tlc.sh" "$W/t/$1.cfg" >"$W/tlc.out" 2>&1
}
mkcfg mut "expect-fail=NoDup" "INVARIANTS\n  TypeOK NoDup"
mkcfg hold "INVARIANT TypeOK\nINVARIANT NoDup NoTorn"
mkcfg live "expect-fail=SlotFree" "PROPERTIES\n  SlotFree"
mkcfg live2 "expect-fail=SlotFree" "PROPERTIES SlotFree Other"
mkcfg bare "expect-fail" "INVARIANT NoDup"
mkcfg unchecked "expect-fail=NoDup" "INVARIANT NoTorn"
tlc mut 12 "Error: Invariant NoDup is violated.";      expect_rc "tlc expected violation" 0 $?
grep -q '|violates NoDup|violates NoDup|ok|42|' "$W/tlc.out" && ok || bad "tlc RESULT row: $(grep RESULT "$W/tlc.out")"
[ -s "$W/traces/mut.txt" ] && ok || bad "tlc trace not written"
tlc mut 12 "Error: Invariant TypeOK is violated.";     expect_rc "tlc wrong invariant" 1 $?
tlc mut 11 "Error: Deadlock reached.";                 expect_rc "tlc deadlock instead of violation" 1 $?
tlc mut 0 "";                                          expect_rc "tlc vacuous mutation" 1 $?
tlc hold 0 "";                                         expect_rc "tlc holds" 0 $?
tlc hold 12 "Error: Invariant NoTorn is violated.";    expect_rc "tlc unexpected violation" 1 $?
tlc live 13 "Error: Temporal properties were violated."; expect_rc "tlc expected liveness" 0 $?
tlc live2 13 "Error: Temporal properties were violated."; expect_rc "tlc unnamed liveness" 1 $?
tlc mut 255 "Error: parse error";                      expect_rc "tlc tool error" 2 $?
tlc bare 12 "Error: Invariant NoDup is violated.";     expect_rc "tlc bare expect-fail" 2 $?
tlc unchecked 12 "Error: Invariant NoDup is violated."; expect_rc "tlc expects an unchecked property" 2 $?
JAVA="$W/nojava" TLA_JAR="$W/tla2tools.jar" TOOLS_LOCK="$W/empty.lock" "$HERE/tla/run_tlc.sh" "$W/t/hold.cfg" >/dev/null 2>&1
expect_rc "tlc java missing" 2 $?
echo "TLA_JAR_SHA256=0000" >"$W/pin.lock"
JAVA="$W/java" TLA_JAR="$W/tla2tools.jar" TOOLS_LOCK="$W/pin.lock" "$HERE/tla/run_tlc.sh" "$W/t/hold.cfg" >/dev/null 2>&1
expect_rc "tlc jar checksum mismatch" 2 $?

# ---- run_cbmc.sh with a fake esbmc -----------------------------------------------------------------------
cat >"$W/esbmc" <<'FAKE'
#!/usr/bin/env bash
[ "${1:-}" = "--version" ] && { echo "ESBMC version ${FAKE_VER:-8.7.0} 64-bit"; exit 0; }
printf '%b\n' "${FAKE_OUT:-}"
exit "${FAKE_RC:-0}"
FAKE
chmod +x "$W/esbmc"; mkdir -p "$W/g"; echo "ESBMC_VERSION=8.7" >"$W/bmc.lock"
mkjob() { printf '%b\n' "TOOL=esbmc\nSOURCES=\"h.cpp\"\n$2" >"$W/g/$1.check"; }
bmc() { # job, rc, output
  FAKE_RC="$2" FAKE_OUT="$3" ESBMC="$W/esbmc" TOOLS_LOCK="$W/bmc.lock" "$HERE/cbmc/run_cbmc.sh" "$W/g/$1.check" >"$W/bmc.out" 2>&1
}
mkjob pass 'EXPECT=pass\nCHECKS="chunks header|payload ok"'
mkjob mut "EXPECT=fail\nEXPECT_PROP='array bounds'\nCHECKS=\"mutation\""
mkjob noprop 'EXPECT=fail\nCHECKS="mutation"'
mkjob unsound 'EXPECT=pass\nFLAGS="--no-unwinding-assertions --unwind 5"'
mkjob waived 'EXPECT=pass\nFLAGS="--no-unwinding-assertions --unwind 5"\nUNWIND_WAIVER="loop bound proven elsewhere"'
mkjob tier 'EXPECT=pass\nTIER=slow'
bmc pass 0 "VERIFICATION SUCCESSFUL";                  expect_rc "bmc pass" 0 $?
[ "$(grep '^RESULT' "$W/bmc.out" | awk -F'|' '{print NF}')" = 10 ] && ok || bad "bmc RESULT fields: $(grep '^RESULT' "$W/bmc.out")"
grep -q '^RESULT|g/pass|esbmc|chunks header/payload ok|pass|pass|' "$W/bmc.out" && ok || bad "bmc separator in CHECKS: $(grep '^RESULT' "$W/bmc.out")"
bmc pass 1 "Violated property:\n  file h.cpp line 5 function f\n  dereference failure: array bounds violated\nVERIFICATION FAILED"; expect_rc "bmc unexpected violation" 1 $?
bmc mut 1 "Violated property:\n  file h.cpp line 5 function f\n  dereference failure: array bounds violated\nVERIFICATION FAILED";  expect_rc "bmc expected violation" 0 $?
bmc mut 1 "Violated property:\n  file h.cpp line 9 function main\n  assertion x\nVERIFICATION FAILED";  expect_rc "bmc violation of another property" 1 $?
bmc mut 0 "VERIFICATION SUCCESSFUL";                   expect_rc "bmc vacuous mutation" 1 $?
bmc noprop 1 "  FAILED\nVERIFICATION FAILED";          expect_rc "bmc mutation without EXPECT_PROP" 2 $?
bmc pass 0 "no body for function foo\nVERIFICATION SUCCESSFUL"; expect_rc "bmc callee without body" 2 $?
bmc pass 3 "segfault";                                 expect_rc "bmc tool error" 2 $?
bmc unsound 0 "VERIFICATION SUCCESSFUL";               expect_rc "bmc pass job without unwinding assertions" 2 $?
bmc waived 0 "VERIFICATION SUCCESSFUL";                expect_rc "bmc waived unwinding assertions" 0 $?
bmc tier 0 "VERIFICATION SUCCESSFUL";                  expect_rc "bmc unknown tier" 2 $?
FAKE_VER=9.1.0 bmc pass 0 "VERIFICATION SUCCESSFUL";   expect_rc "bmc version mismatch" 2 $?

echo "formal selftest: $N checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
