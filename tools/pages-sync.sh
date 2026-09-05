#!/usr/bin/env bash
# Mirror docs/presentation/ into the gh-pages branch and commit.
#
# The published site (https://dk5en.github.io/MeshCom-Firmware/) is the
# gh-pages branch; its source of truth is docs/presentation/ on fork-main.
# This script copies the directory verbatim into a temporary worktree of
# gh-pages, adds .nojekyll, commits when something changed, and prints the
# push command. It never pushes by itself.
#
# Usage: tools/pages-sync.sh [-m "commit message"] [--push]
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
src="$repo/docs/presentation"
branch="gh-pages"
msg=""
push=0
while [ $# -gt 0 ]; do
  case "$1" in
    -m) msg="$2"; shift 2 ;;
    --push) push=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -d "$src" ] || { echo "missing $src" >&2; exit 1; }
git -C "$repo" show-ref --verify --quiet "refs/heads/$branch" || { echo "no local branch $branch" >&2; exit 1; }

src_head="$(git -C "$repo" rev-parse --short HEAD)"
[ -n "$msg" ] || msg="docs(pages): sync docs/presentation from $(git -C "$repo" rev-parse --abbrev-ref HEAD)@$src_head"

wt="$(mktemp -d "${TMPDIR:-/tmp}/pages-sync.XXXXXX")"
cleanup() { git -C "$repo" worktree remove --force "$wt" 2>/dev/null || true; rm -rf "$wt"; }
trap cleanup EXIT
git -C "$repo" worktree add --quiet "$wt" "$branch"

# Full mirror: whatever is not in docs/presentation disappears from the site.
find "$wt" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
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
