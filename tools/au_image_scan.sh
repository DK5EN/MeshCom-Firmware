#!/bin/sh
# String scan of built images for the Auto Update web GUI campaign (spec 10.1).
# Usage: tools/au_image_scan.sh [--instrument] <env> [<env> ...]   (images from .pio/build/<env>/firmware.elf)
# --instrument: INSTRUMENT image, expects the test commands instead of their absence.
INSTR=0
[ "$1" = "--instrument" ] && { INSTR=1; shift; }
fail=0
has() { strings "$1" | grep -qF -- "$2"; }
for env in "$@"; do
    elf=".pio/build/$env/firmware.elf"
    [ -f "$elf" ] || { echo "FAIL $env: $elf missing"; fail=1; continue; }
    check() { # want(1|0) needle
        if has "$elf" "$2"; then got=1; else got=0; fi
        if [ "$got" != "$1" ]; then echo "FAIL $env: '$2' present=$got want=$1"; fail=1; fi
    }
    if [ "$env" = "wiscore_rak4631" ]; then
        check 0 "[AU]"; check 0 "austatus"
    else
        check 1 "austatus"; check 1 "[AU];auto;skip;"; check 1 "[AU];auto;blocked;notify only"
        check 1 "off removed, use notify"
        check 0 ">off</option>"
        if [ "$INSTR" = 1 ]; then
            check 1 "[AU];heapgate;set"; check 1 "[AU];checknow;armed"; check 1 "[AU];forget;"
        else
            check 0 "[AU];stagelan;started"; check 0 "[AU];heapgate;"; check 0 "[AU];checknow;"; check 0 "[AU];forget;"
        fi
    fi
    [ "$fail" = 0 ] && echo "ok   $env"
done
exit $fail
