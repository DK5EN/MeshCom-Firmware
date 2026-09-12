#!/bin/sh
# Gate for the golden-capture tooling. Every tool here carries its own tests
# against real repository fixtures, not against invented input -- run them all
# before trusting a capture produced with them.
#
#   sh test/golden/selftest.sh
set -e
cd "$(dirname "$0")/../.."

python3 test/golden/mc_frame.py
python3 test/golden/normalize.py --self-test
python3 test/golden/corpus_lint.py --self-test
python3 test/golden/extract_commands.py --self-test
python3 test/golden/build_corpus.py --self-test
python3 test/golden/backup_nodes.py --self-test
python3 test/golden/backup_nodes.py --verify-masked
python3 tools/bench/ble_golden.py --self-test
python3 test/golden/command_name_scan.py --self-test
python3 test/golden/compare_udp.py --self-test
python3 test/golden/compare_extudp.py --self-test
python3 test/golden/radio_units_lint.py --self-test
python3 test/golden/radio_units_lint.py
python3 test/golden/twin_stub_lint.py --self-test
python3 test/golden/twin_stub_lint.py
python3 test/golden/carve_extern_lint.py --self-test
python3 test/golden/carve_extern_lint.py
python3 test/golden/command_ladder_lint.py --self-test
python3 test/golden/command_ladder_lint.py
python3 test/golden/settings_layout_lint.py --self-test
python3 test/golden/settings_layout_lint.py
python3 test/golden/variant_macros_lint.py --self-test
python3 test/golden/variant_macros_lint.py
python3 test/golden/drift_matrix_lint.py --self-test
# --phase pre-review is TEMPORARY: it downgrades the empty-verdict check while
# the phase-2 HITL review session is still owed. Drop the flag the day the
# verdicts are filled in -- testplan section 5.3 requires a verdict on every row
# before the first unification commit, and leaving this flag here would let
# that requirement quietly expire.
python3 test/golden/drift_matrix_lint.py --phase pre-review
python3 test/golden/verify_captures.py
python3 test/golden/corpus_lint.py test/golden/corpus/
python3 -m unittest discover tools/mock 2>&1 | tail -3
