#!/bin/bash
# Derive fork-neo from fork-neo-test.
#
# fork-neo is not maintained -- it is a function of fork-neo-test. This script
# throws the branch away and rebuilds it from upstream/dev by projecting the
# chapter path lists next to this file, then applying tools/neo_strip.py inside
# the core commit. NEVER commit to fork-neo: the next run of this script will
# drop it without warning. See docs/neo-campaign.md section 8, model D.
#
#   tools/neo/derive.sh [SRC] [TARGET] [BASE]
#
# Defaults: SRC=fork-neo-test  TARGET=fork-neo  BASE=upstream/dev
set -e
cd "$(git rev-parse --show-toplevel)"
SRC=${1:-fork-neo-test}
TARGET=${2:-fork-neo}
BASE=${3:-upstream/dev}
CO="Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
MSG=$WORK/msg

# The path lists live in the tree this script is about to check out from under
# itself -- upstream/dev has no tools/neo. Snapshot them first.
HERE=$WORK/paths
mkdir -p "$HERE"
cp tools/neo/paths/*.txt "$HERE/"
# Same for the strip itself. It runs against the working tree but must NOT end
# up on the branch: fork-neo does not advertise that it is a derivative. It is
# upstream/dev plus better code, and the test scaffolding was never part of
# what is offered.
STRIP=$WORK/neo_strip.py
cp tools/neo_strip.py "$STRIP"

[ -z "$(git status --porcelain | grep -v '^??')" ] || { echo "ABORT: working tree not clean"; exit 1; }
git rev-parse --verify -q "$SRC" >/dev/null || { echo "ABORT: $SRC does not exist"; exit 1; }
if git rev-parse --verify -q "$TARGET" >/dev/null; then
  old=$(git rev-parse --short "$TARGET")
  tag="neo-backup-$(date +%Y%m%d%H%M)-$TARGET"
  git tag "$tag" "$TARGET"
  git branch -D "$TARGET"
  echo "old $TARGET ($old) kept as $tag"
fi

git checkout -q -b "$TARGET" "$BASE"
echo "$TARGET from $BASE ($(git rev-parse --short HEAD))"

git checkout --no-overlay "$SRC" -- docs/CHANGELOG-neo.md
git commit -q -m "docs(neo): das Changelog

Siebzehn Kapitel, fuenf Commits. Die Abbildung steht im Kopf des Dokuments.

$CO"

for g in K01 CORE K15 K16 K17; do
  git checkout --no-overlay "$SRC" --pathspec-from-file="$HERE/$g.txt"
  case "$g" in
    # platformio.ini arrives with CORE and names the Tasmota script as a
    # pre-step of both safeboot envs, so the script has to arrive with it.
    CORE) git checkout --no-overlay "$SRC" -- tools/ensure_tasmota_framework.py
          python3 "$STRIP"
          git add -- platformio.ini src/t-deck/tdeck_helpers.cpp ;;
    K15)  git checkout --no-overlay "$SRC" -- safeboot.bin safeboot-s3.bin ;;
  esac
  n=$(git diff --cached --name-only --no-renames | wc -l | tr -d ' ')
  if [ "$g" = "CORE" ]; then
    head="neo: der verflochtene Kern -- Konfiguration, Persistenz, Transport, Typwechsel"
  else
    head="neo $g: $(grep -m1 "^## $g " docs/CHANGELOG-neo.md | sed "s/^## $g //")"
  fi
  printf '%s\n\n%s\n\n%s\n' "$head" "$n Pfade." "$CO" > "$MSG"
  git commit -q -F "$MSG"
  echo "  $g  $n paths -> $(git rev-parse --short HEAD)"
done

echo
echo "=== gate: strip identity ==="
git checkout -q -b neo-stripcheck "$SRC"
python3 "$STRIP" > /dev/null
git add -- platformio.ini src/t-deck/tdeck_helpers.cpp
git commit -q -m "throwaway: strip"
R=0
if git diff --quiet "$TARGET" neo-stripcheck -- src lib variants config platformio.ini; then
  echo "   empty   strip($SRC) == $TARGET over src lib variants config platformio.ini"
else
  echo "   DIFF:"; git diff --stat "$TARGET" neo-stripcheck -- src lib variants config platformio.ini; R=1
fi
for b in safeboot.bin safeboot-s3.bin; do
  if [ "$(git rev-parse "$TARGET:$b")" = "$(git rev-parse "$SRC:$b")" ]
    then echo "   same    $b"
    else echo "   DIFF    $b"; R=1; fi
done
git checkout -q "$TARGET"
git branch -q -D neo-stripcheck
exit $R
