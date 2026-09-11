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
python3 test/golden/corpus_lint.py test/golden/corpus/
python3 -m unittest discover tools/mock 2>&1 | tail -3
