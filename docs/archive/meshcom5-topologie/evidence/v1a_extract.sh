#!/bin/bash
S=/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/d02d6afd-b7bd-4aaa-85ff-36d56f056561/scratchpad/v1a
mkdir -p "$S"
cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main || exit 1
for t in v4.35d v4.35e v4.35k v4.35n v4.35p v4.35p.05.08 v4.35p.06.08 v4.35p.06.10 v4.35s v4.35t upstream/dev 2c96f11b^ 2c96f11b 082c2412; do
  n=$(echo "$t" | tr '/^' '_x')
  for f in aprs_functions.cpp lora_functions.cpp via_functions.cpp loop_functions.cpp udp_functions.cpp esp32/esp32_main.cpp; do
    b=$(basename "$f")
    git show "$t:src/$f" > "$S/$n.$b" 2>/dev/null
  done
  for f in $(git ls-tree -r --name-only "$t" src | grep -E 'udp_frame|esp32_udp|nrf52_udp|udp_func'); do
    b=$(basename "$f")
    git show "$t:$f" > "$S/$n.$b" 2>/dev/null
  done
done
ls "$S"
