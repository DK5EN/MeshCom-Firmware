#!/bin/bash
# mkdir lock around one pio call (macOS has no flock); serialises pio across parallel writers.
# Usage: tools/piolock.sh pio test -e <env> ...
L=/tmp/meshcom-pio.lock
while ! mkdir "$L" 2>/dev/null; do
  pid=$(cat "$L/pid" 2>/dev/null)
  if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then rm -rf "$L"; continue; fi
  sleep 2
done
echo $$ > "$L/pid"
trap 'rm -rf "$L"' EXIT
"$@"
