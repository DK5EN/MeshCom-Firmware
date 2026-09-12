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
# --phase implementation is TEMPORARY. The M2 review happened on 2026-09-12 and
# filled all 29 verdicts, so the empty-verdict check is now enforced HARD and
# --phase pre-review is obsolete. What this phase still downgrades is the
# asserting_test column: 10 rows are decided but not yet implemented, so the
# test that would fail does not exist yet. Drop the flag -- run this line with
# no --phase at all -- once the unification waves have written them. A phase
# flag left in place after its window has passed is how a requirement quietly
# expires, which is why the script prints the count on every run.
python3 test/golden/drift_matrix_lint.py --phase implementation
python3 test/golden/verify_captures.py
python3 test/golden/corpus_lint.py test/golden/corpus/
python3 -m unittest discover tools/mock 2>&1 | tail -3
