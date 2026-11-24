#!/bin/sh
# Line coverage of src/ by the native Unity envs (TESTGATE-01 step 4.4). Opt-in:
# no gate step runs it.
#
#   tools/coverage_native.sh                    # every [env:native*]
#   tools/coverage_native.sh native_aprs ...    # only these envs
#
# It builds into .pio/build-cov, so the normal build directories and their
# warm objects stay untouched. The report lands in
# tools/bench/runs/coverage-<stamp>/ (coverage.txt per file with the missing
# lines, coverage.json as summary).
#
# Reading the number: it counts lines of the src/ files that some native env
# compiles or includes. A src/ file no env touches is absent from the table,
# not 0 %; the last line of the output says how many those are.
#
# How the flags get in: PLATFORMIO_BUILD_FLAGS reaches every env without an
# edit to platformio.ini. SCons passes --coverage to the compiler only, so the
# profile runtime is linked as a library (-L/-l do reach the linker).
set -u
cd "$(dirname "$0")/.."

if pgrep -f 'pio (test|run|project)' >/dev/null 2>&1; then
    echo "another pio process is running -- one pio at a time" >&2; exit 2
fi
RT="$(clang --print-resource-dir 2>/dev/null)/lib/darwin"
if [ ! -f "$RT/libclang_rt.profile_osx.a" ]; then
    echo "no clang profile runtime at $RT (macOS clang expected)" >&2; exit 2
fi

envs="$*"
[ -n "$envs" ] || envs=$(grep -oE '^\[env:native[^]]*\]' platformio.ini | tr -d '[]' | sed 's/^env://')
args=""; for e in $envs; do args="$args -e $e"; done

OUT="tools/bench/runs/coverage-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
BUILD="$(pwd)/.pio/build-cov"
# Counters of an earlier run would add up with this one.
find "$BUILD" -name '*.gcda' -delete 2>/dev/null

export PLATFORMIO_BUILD_DIR="$BUILD"
export PLATFORMIO_BUILD_FLAGS="--coverage -L$RT -lclang_rt.profile_osx"
# shellcheck disable=SC2086
pio test $args > "$OUT/pio-test.log" 2>&1; rc=$?
if [ "$rc" != 0 ]; then
    echo "pio test exit $rc -- the report covers what ran; see $OUT/pio-test.log" >&2
fi

# The replay suites run single lines more than 2^32 times; gcovr takes such a count for a
# gcc bug unless the check is switched off.
uv run --quiet --with gcovr==8.8 gcovr -r . --filter 'src/' \
    --gcov-executable 'xcrun llvm-cov gcov' --gcov-suspicious-hits-threshold 0 "$BUILD" \
    --txt "$OUT/coverage.txt" --json-summary "$OUT/coverage.json" > "$OUT/gcovr.log" 2>&1 || {
    echo "gcovr failed -- $OUT/gcovr.log" >&2; exit 1; }

tail -3 "$OUT/coverage.txt" | head -1
seen=$(grep -cE '^src/' "$OUT/coverage.txt")
all=$(find src -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' \) | wc -l | tr -d ' ')
echo "src files in the report: $seen of $all (the rest is compiled by no native env)"
echo "report: $OUT/coverage.txt"
exit "$rc"
