#!/bin/sh
# Re-run every hb9 check. usage: run_all.sh <norm_dir> <out_dir>
# <norm_dir> holds hb9vqq4.log and hb9vqq5.log (normalised logs); outputs (json, jsonl) go to <out_dir>.
set -eu
N="$1"
O="$2"
H="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$O"
for n in 4 5; do
  if [ "$n" = 4 ]; then own=HB9VQQ-4; par=HB9VQQ-5; else own=HB9VQQ-5; par=HB9VQQ-4; fi
  python3 -I "$H/ring_fate.py" "$N/hb9vqq$n.log" "$O/fate$n.json" "$own" "$par" > /dev/null
  python3 -I "$H/node_checks.py" "$N/hb9vqq$n.log" "$O/node$n.json" "$own" > /dev/null
  python3 -I "$H/decay_checks.py" "$N/hb9vqq$n.log" "$O/fate${n}_writes.jsonl" "$O/decay$n.json" > /dev/null
  python3 -I "$H/slow_waits.py" "$N/hb9vqq$n.log" "$O/fate${n}_writes.jsonl" "$O/slow$n.json" 100 10 > /dev/null
  python3 -I "$H/service_rate.py" "$N/hb9vqq$n.log" "$O/svc$n.json" > /dev/null
  python3 -I "$H/idreuse_check.py" "$N/hb9vqq$n.log" "$O/fate${n}_writes.jsonl" "$O/idreuse$n.json" > /dev/null
  python3 -I "$H/ferr_by_lasthop.py" "$N/hb9vqq$n.log" "$O/ferr$n.json" > /dev/null
done
python3 -I "$H/pair_checks.py" "$O/fate4_writes.jsonl" "$O/fate5_writes.jsonl" "$O/pair.json" > /dev/null
python3 -I "$H/interferer_check.py" "$N/hb9vqq4.log" "$O/intf4.json" > /dev/null
python3 -I "$H/crc_source_check.py" "$N/hb9vqq4.log" "$O/crcsrc4.json" > /dev/null
# NOTE: the *_writes.jsonl intermediates are deleted after the run in the committed copy (regenerate with this script).
echo done
