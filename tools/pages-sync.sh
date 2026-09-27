#!/usr/bin/env bash
# Mirror docs/presentation/ into the gh-pages branch and commit.
#
# The published site (https://dk5en.github.io/MeshCom-Firmware/) is the
# gh-pages branch; its source of truth is docs/presentation/ on the working
# branch. This script copies the directory into a temporary worktree of
# gh-pages, adds .nojekyll, commits when something changed, and prints the
# push command. It never pushes by itself.
#
# Mirror, with one exception: top-level paths in KEEP belong to other tools
# and are never touched. flash/ is the web flasher, written only by
# tools/pages_flasher.py publish; it is not in docs/presentation/, so a plain
# mirror deleted all firmware images from the site.
#
# Usage: tools/pages-sync.sh [-m "commit message"] [--push]
#        tools/pages-sync.sh --self-test
set -euo pipefail

KEEP=(flash)

self_test() {
  local tmp script
  script="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/pages-sync-test.XXXXXX")"
  trap 'rm -rf "$tmp"' RETURN
  (
    set -e
    cd "$tmp"
    git init --quiet -b main .
    git config user.email test@example.invalid
    git config user.name test
    mkdir -p docs/presentation
    echo new > docs/presentation/index.html
    git add -A && git commit --quiet -m src
    git checkout --quiet --orphan gh-pages
    git rm -rf --quiet . >/dev/null
    mkdir -p flash/v1
    echo image > flash/v1/firmware.bin
    echo old > index.html
    echo stale > stale.html
    git add -A && git commit --quiet -m pages
    git checkout --quiet main
    "$script" -m test >/dev/null
    tree="$(git ls-tree -r --name-only gh-pages)"
    echo "$tree" | grep -qx 'flash/v1/firmware.bin' || { echo "FAIL: flash/ was deleted"; exit 1; }
    echo "$tree" | grep -qx 'stale.html' && { echo "FAIL: stale.html not removed"; exit 1; }
    [ "$(git show gh-pages:index.html)" = new ] || { echo "FAIL: index.html not updated"; exit 1; }
    echo "$tree" | grep -qx '.nojekyll' || { echo "FAIL: .nojekyll missing"; exit 1; }
    mkdir -p docs/presentation/flash
    echo bad > docs/presentation/flash/x
    if "$script" -m test >/dev/null 2>&1; then echo "FAIL: flash/ in the source was accepted"; exit 1; fi
  )
  echo "self-test OK"
}

repo="$(git rev-parse --show-toplevel)"
src="$repo/docs/presentation"
branch="gh-pages"
msg=""
push=0
while [ $# -gt 0 ]; do
  case "$1" in
    -m) msg="$2"; shift 2 ;;
    --push) push=1; shift ;;
    --self-test) self_test; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -d "$src" ] || { echo "missing $src" >&2; exit 1; }
git -C "$repo" show-ref --verify --quiet "refs/heads/$branch" || { echo "no local branch $branch" >&2; exit 1; }
for k in ${KEEP[@]+"${KEEP[@]}"}; do
  [ ! -e "$src/$k" ] || { echo "$src/$k exists, but $k/ belongs to another tool on $branch -- refusing" >&2; exit 1; }
done

src_head="$(git -C "$repo" rev-parse --short HEAD)"
[ -n "$msg" ] || msg="docs(pages): sync docs/presentation from $(git -C "$repo" rev-parse --abbrev-ref HEAD)@$src_head"

wt="$(mktemp -d "${TMPDIR:-/tmp}/pages-sync.XXXXXX")"
cleanup() { git -C "$repo" worktree remove --force "$wt" 2>/dev/null || true; rm -rf "$wt"; }
trap cleanup EXIT
git -C "$repo" worktree add --quiet "$wt" "$branch"

# Mirror: whatever is not in docs/presentation disappears from the site,
# except the KEEP paths (and .git).
prune=(find "$wt" -mindepth 1 -maxdepth 1 ! -name .git)
for k in ${KEEP[@]+"${KEEP[@]}"}; do prune+=(! -name "$k"); done
"${prune[@]}" -exec rm -rf {} +
cp -R "$src"/. "$wt"/
touch "$wt/.nojekyll"

git -C "$wt" add -A
if git -C "$wt" diff --cached --quiet; then
  echo "gh-pages already matches docs/presentation -- nothing to commit"
  exit 0
fi
git -C "$wt" commit --quiet -m "$msg" -m "Source: docs/presentation @ $src_head"
echo "committed on $branch: $(git -C "$wt" rev-parse --short HEAD)"
if [ "$push" = 1 ]; then
  git -C "$repo" push origin "$branch"
else
  echo "publish with: git push origin $branch"
fi
