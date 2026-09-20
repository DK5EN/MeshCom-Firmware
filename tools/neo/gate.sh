#!/bin/bash
# Gate for the derived fork-neo: every commit builds on all eight lead targets
# with its DRAM/IRAM headroom checked, then the defined-symbol sets of the two
# branches are compared at their tips.
#
#   tools/neo/gate.sh LOGFILE [TARGET] [SRC] [BASE]
#
# Expect 38/40, not 40/40: K01 carries only upstream's code plus the campaign's
# deletions, and upstream ships ttgo_tbeam with 20 bytes of free iram0_0_seg and
# E22_XML-DevKitC with 1160 B of DRAM. Both link; what fails is the headroom
# rule. Everything from the core commit onward must be green.
cd "$(git rev-parse --show-toplevel)"
PIO=${PIO:-$HOME/.platformio/penv/bin/pio}
LOG="${1:?usage: tools/neo/gate.sh LOGFILE [TARGET] [SRC] [BASE]}"; : > "$LOG"
TARGET=${2:-fork-neo}
SRC=${3:-fork-neo-test}
BASE=${4:-upstream/dev}
SYM=$(mktemp -d); trap 'rm -rf "$SYM"' EXIT
# resource_watch.py is a fork tool: it does not exist on upstream/dev and is not
# part of the chapter cut, so it is absent from every commit this gate checks
# out. Snapshot it before the first checkout -- otherwise the region check
# silently does nothing and the gate reports all green.
RW=$SYM/resource_watch.py
cp tools/resource_watch.py "$RW"
ENVS="heltec_wifi_lora_32_V3 E22-DevKitC E22_XML-DevKitC ttgo_tbeam ttgo_tbeam_supreme t_deck t_deck_plus wiscore_rak4631"
say(){ echo "$@" | tee -a "$LOG"; }
nm_for(){ case "$1" in
  heltec*|t_deck*|ttgo_tbeam_supreme) echo ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm;;
  wiscore*) echo ~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm;;
  *) echo ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm;; esac; }

say "### $TARGET: build per commit  $(date +%H:%M:%S)"
p=0; f=0
while read -r sha subj; do
  git checkout -q "$sha"
  say ""
  say "=== $(echo "$sha" | cut -c1-8)  $subj"
  for e in $ENVS; do
    if "$PIO" run -e "$e" >"$LOG.b" 2>&1; then
      if reg=$(python3 "$RW" dram --env "$e" --map ".pio/build/$e/firmware.map" --min-headroom 4000 --strict 2>&1)
        then say "   ok   $e"; p=$((p+1))
        else say "   REGION $e"; echo "$reg" | sed 's/^/      /' >> "$LOG"; f=$((f+1)); fi
    else
      say "   FAIL $e"; grep -m3 -E "error:|undefined reference|multiple definition" "$LOG.b" | sed 's/^/      /' >> "$LOG"; f=$((f+1))
    fi
  done
done < <(git log --reverse --format="%H %s" "$BASE..$TARGET" | grep -v "docs(neo)")
say ""
say "$TARGET: $p ok / $f fail"

say ""
say "### symbol diff against $SRC  $(date +%H:%M:%S)"
for br in "$TARGET" "$SRC"; do
  git checkout -q "$br"
  for e in $ENVS; do
    "$PIO" run -e "$e" >/dev/null 2>&1 &&
      $(nm_for "$e") --defined-only ".pio/build/$e/firmware.elf" 2>/dev/null |
      awk '{print $NF}' | sort -u > "$SYM/$e.$br"
  done
done
d=0
for e in $ENVS; do
  a=$(comm -23 "$SYM/$e.$TARGET" "$SYM/$e.$SRC" | wc -l | tr -d ' ')
  b=$(comm -13 "$SYM/$e.$TARGET" "$SYM/$e.$SRC" | wc -l | tr -d ' ')
  if [ "$a" = "0" ] && [ "$b" = "0" ]
    then say "   same    $e  ($(wc -l < "$SYM/$e.$TARGET" | tr -d ' ') symbols)"
    else say "   DIFF    $e  only in $TARGET: $a, only in $SRC: $b"
         comm -3 "$SYM/$e.$TARGET" "$SYM/$e.$SRC" | head -10 | sed 's/^/      /' >> "$LOG"; d=$((d+1)); fi
done
say ""
say "=== overall: builds $p/$((p+f)), symbol deviations on $d of 8 targets === $(date +%H:%M:%S)"
[ "$d" = "0" ]
