---
description: Cut and publish a complete MeshCom fork release - docs, gates, commit, v* tag (GitHub CI builds and publishes 66 assets), asset verification, web flasher on GitHub Pages
allowed-tools: [Bash, Read, Edit, Write, Glob, Grep]
---

Cut a full stability release of this fork and publish it on GitHub.

**Since v4.40a.10.11 (2026-10-11) GitHub CI builds and publishes the release.**
The `MeshCom CI` workflow is enabled again and fires on tags `v*` (release,
becomes Latest, exports `MC_BUILD_TAG`) and `ci-proof-*` (draft, never Latest;
delete draft + tag afterwards). It builds all default envs, writes the 27
`.autoupdate` twins with `make_autoupdate.py --check` and attaches all 66 assets.
So building, asset assembly and publishing are done by CI; you still
do Steps 1-2 (docs, gate), push `fork-dev`, push the tag, then watch the run
(`gh run watch <id> -R DK5EN/MeshCom-Firmware --exit-status`) and verify the
release (66 assets, `.autoupdate` count 27, `78 da` headers, re-inflate check on
the downloaded assets, the tag string in one image). The web flasher (Step 6)
needs a local build of the same commit for bootloader/partitions/safeboot, and
takes the app images from the release:
`gh release download <tag> -R DK5EN/MeshCom-Firmware -D <dir>`, then
`uv run tools/pages_flasher.py publish --version <tag> --keep 1 --from-release <dir> [--push]`
and `uv run tools/pages_flasher.py check --version <tag> --from-release <dir>`.
The `ci-build.yml` push workflow stays disabled. The Fallback appendix below
covers a local build when CI is unavailable.

## Ground rules learned the hard way

- **`gh` resolves to the upstream repo by default** (this is a fork of
  icssw-org/MeshCom-Firmware). EVERY `gh release`/`gh api` call MUST carry
  `-R DK5EN/MeshCom-Firmware`, or you will read — or worse, delete — upstream
  releases. `gh release list` without `-R` shows upstream's releases and has
  fooled us before.
- **Release object and git tag are independent.** Deleting a release
  (`gh release delete <tag> -R DK5EN/MeshCom-Firmware --yes`) keeps the tag
  unless you add `--cleanup-tag`. Ask the user which of the two they want
  gone before deleting anything.
- Releases go on the current working branch's HEAD. Do not merge or switch
  branches for a release. The working branch is `fork-dev` (primary since
  2026-10-02: `upstream/dev` plus fork docs, tests, tools, GitHub Pages and
  fork-only firmware, roughly 200 commits ahead; `fork-neo-test` is retired as
  tag `archive/fork-neo-test-20261002`, `fork-main` as
  `archive/fork-main-20260927`). Older docs that say `fork-neo-test`,
  `fork-main` or `v4.35p_prio` describe retired lines. A fork release is the
  fork-dev build, which carries firmware fixes upstream does not have
  yet, so it is NOT byte-identical to the official upstream release of the
  same version -- say so in release-notes.md.

## Versioning

- Scheme: `v<VER>.MM.DD` (no suffix -- the `-stability` suffix was dropped
  on 2026-09-05; every tag up to and including `v4.35s.09.03-stability`
  carries it, nothing newer does), where `<VER>` is the firmware version the
  tree currently carries -- read it from `SOURCE_VERSION` +
  `SOURCE_VERSION_SUB` in `src/configuration_global.h`, never from an older
  tag. The letter follows upstream and changes without notice (4.35p ->
  4.35s on 2026-09-03). **Never use a `.N` suffix:** `fwParseTag()` accepts
  only `v<VER>` and `v<VER>.MM.DD`, so a `.2` tag is invisible to every node's
  Auto Update. Date a same-day re-cut to the next day instead (precedent:
  v4.40a.10.06, published 2026-10-05); `FLASH_VERSION` and `--date` follow the
  tag date. (Pre-AU tags such as v4.35p.08.27.2-stability carry `.2`; that is
  history, not a pattern.) Ask the user for the tag name if there is any
  ambiguity (same-day re-release vs. replace-in-place).
- **Check upstream's tags before naming ours**: `git ls-remote --tags upstream`.
  Upstream uses the same `v<VER>.MM.DD` shape since 2026-09-12 (`v4.35t.09.12`
  on their `dev` merge of our PR #1140), and since 4.40a the bare `v<VER>`
  (`v4.40a` = their merge of our PR #1186). A fork tag with an upstream tag's
  name clobbers on every fetch, so never reuse a bare upstream tag: use
  `v<VER>.MM.DD` (precedent: `v4.40a.10.02`). The `-neo` suffix is retired.
  Also `git fetch upstream --tags` first so the upstream name exists locally
  and nothing is pushed under it later.
- The letter no longer marks the fork: upstream released official `v4.35t` on
  2026-09-10 with our PR #1135 (items 104-210) inside. Release text must frame
  the delta against the official release that actually exists (check
  `gh release list -R icssw-org/MeshCom-Firmware`), and point at the flash
  stamp in `--info` as the distinguishing mark.
- The GitHub release title is the bare tag name. The release-notes headings
  and the `## New in <tag>` changelog sections use the bare tag too.
- `FLASH_VERSION` in `src/configuration_global.h`: bump to the release date
  (`YYYYMMDD`) of the tag. It is purely informative since 3a31317b.
  `FLASH_STRUCT_VERSION` moves ONLY on a real settings-layout change — it
  wipes fleet configs, never touch it casually.

## Step 1 — Release documents

Two files are updated for every release; a topic changelog only when the
content fits one:

- `release-notes.md` (repo root, **English**) — the GitHub release body and
  nothing else; only the current release. Keep the structure: `[!IMPORTANT]`
  not-official box, "What this release is", changelog/PR-draft links (they
  embed the tag name — update to the new tag!), "What changes on the air",
  "Supported Hardware" split _bench-tested_ vs _built and shipped, not on
  our bench_, "Known gaps, stated plainly" (list every open defect honestly,
  e.g. TM-49), "Installing", "Upstream". The README link also embeds the tag.
- `docs/release-journal.md` (**German, no umlauts** — write `ae/oe/ue`, `--`
  for em dashes) — running journal; moved here on 2026-10-02 because the root
  `release.md` is upstream's own file again and must stay untouched. New section at the TOP, below the header
  that names FLASH_VERSION. Must end with "Was fuer dieses Release auf
  Hardware geprueft wurde" and "Was ausdruecklich NICHT geprueft wurde".
- Topic changelog, only if the release's content fits one
  (`docs/CHANGELOG-snf.md`, `docs/CHANGELOG-neo.md`,
  `docs/CHANGELOG-meshcom5.md`) — update it and list it under
  release-notes.md's "Changelogs" section. `docs/CHANGELOG-neo.md` is
  **German, no umlauts**, one `## <tag>: <Titel>` heading per release, newest
  first. `docs/CHANGELOG-stability.md` is retired for the neo line (stale
  since 2026-09-12.2, per its own header) — do not append to it; on the rare
  release that still needs it, continue numbering from its actual current
  highest item, checked fresh (`grep -oE '^[0-9]+\.' docs/CHANGELOG-stability.md
| sort -n | tail -1` — 221 as of 2026-09-28), never a number remembered from
  an earlier release. A release that fits no topic changelog updates neither
  it nor `docs/CHANGELOG-stability.md` — `release-notes.md` and `docs/release-journal.md`
  alone are enough.
  Before writing "what changed", check completeness, not just plausibility:

```
git log --oneline <prev-tag>..HEAD -- src variants platformio.ini
```

Every commit this lists must show up in release-notes.md, even folded into
one bullet — a scope claim not checked against the actual range is not
trustworthy (the 09.28.2 release notes said "a single fix" but the tag also
shipped `dbc57632`).

Then: `npx --yes prettier@3 --write release-notes.md docs/release-journal.md` (add the
topic changelog too if you touched it). A `.prettierignore` already protects
binaries.

## Step 2 — Gate (must be green before the commit)

```
tools/regression.sh --stage 1,2
```

That runs every `[env:native*]` Unity env, the golden selftest, pytest and the
tool suites (inventory: `docs/test-suite-map.md`). **Never** bare `pio test`
(it walks the board envs and flashes attached hardware), and only one `pio`
process at a time.

Copy the total case count this run actually reports into release-notes.md —
never a count remembered from an earlier release.

Idle-heap step (bench, T-Beam DK5EN-92 on the release image of the tag; skip
only if the T-Beam is unreachable, and say so in the release notes):

```
MC_CONSOLE_PW=<console password> python3 tools/bench/heap_idle.py \
  --host <T-Beam IP> --baseline tools/heap_baseline.json --board ttgo_tbeam
```

Exit 1 means idle free heap fell more than 3072 B below the baseline: stop
and find the cause before tagging. The idle figure is the 600 s STAT heap
with BLE up (baseline 69332 B, 2026-10-18; it moves about ±1.3 KB between
the 300 s and 600 s samples). Release images only — INSTRUMENT builds sit
about 3.5 KB lower. Run `--update-baseline` only after a deliberate RAM
change, and commit `tools/heap_baseline.json` with it.

## Step 3 — Commit and push `fork-dev`

Commit the Step 1 documents (`release-notes.md` must be in the tagged commit:
CI reads it from the tag checkout) and push:

```
git push origin fork-dev
```

If `safeboot.bin`/`safeboot-s3.bin` show as modified after a local build, that
is usually header-only noise (ESP32 builds are not byte-reproducible); discard
it with `git checkout -- safeboot.bin safeboot-s3.bin` unless a difference
outside the `esp_app_desc` `app_elf_sha256` field (bytes 177-208) plus its
trailing digest, or a size change, shows real staleness — then stop and
investigate.

## Step 4 — Tag and push (publishes immediately)

```
git tag -a <tag> -m "<one-line summary>"
git push origin <tag>
```

**Pushing a `v*` tag immediately publishes a public Latest release** with 66
assets and sends every node's Auto Update a new target. It needs the operator's
explicit go, every time. Create the tag only on the committed, pushed tree of
Step 3. Moving a pushed tag (`git tag -d`, `git push origin :refs/tags/<tag>`,
re-tag, re-push) is a last resort for a real problem and needs the operator's
on-the-spot consent.

`ci-proof-*` tags build the same assets as a draft that is never Latest. Delete
a proof draft and its tag afterwards, only after confirming with the operator:

```
gh release delete <ci-proof-tag> -R DK5EN/MeshCom-Firmware --cleanup-tag --yes
```

## Step 5 — Watch CI and verify the release

```
gh run list -R DK5EN/MeshCom-Firmware --workflow "MeshCom CI" -L 1
gh run watch <id> -R DK5EN/MeshCom-Firmware --exit-status
```

Then download the assets into a fresh directory and check them:

```
gh release download <tag> -R DK5EN/MeshCom-Firmware -D <dir>
gh release view <tag> -R DK5EN/MeshCom-Firmware --json assets,isDraft --jq '{assets:(.assets|length),isDraft}'
```

- **66 assets, not draft**: 27 ESP32 app `.bin`, 27 `.autoupdate` twins, the
  nRF52 files (3 boards, each `.uf2` and `.zip`) and 6 support files
  (`bootloader.bin`, `bootloader-s3.bin`, `partitions.bin`, `otadata.bin`,
  `safeboot.bin`, `safeboot-s3.bin`). The canonical list is the `artifacts:`
  list in `.github/workflows/meshcom-ci.yml`. The name list must match the
  previous release apart from intended changes:
  `diff <(gh release view <prev-tag> -R DK5EN/MeshCom-Firmware --json assets --jq '.assets[].name' | sort) <(ls <dir> | sort)`
- **Auto Update**: `ls <dir>/*.autoupdate | wc -l` = 27 (one per ESP32 app
  `.bin`); every file starts with the zlib header `78 da` (`xxd -l2 <file>`);
  each is well below its `.bin` (about 64 %, Heltec V3: 1,560,864 B to
  993,337 B). `python3 tools/make_autoupdate.py --check <dir>/<app>.bin ...`
  on the app images (never the support files) re-inflates byte-exact.
- **Tag string**: `strings <dir>/heltec_wifi_lora_32_V3.bin | grep -F '<tag>'`
  (`MC_BUILD_TAG` is set by CI for `v*` tags).
- **Field-command scan (INS-01/INS-04)**: a compile-guard change can drop
  shipped commands without a compiler or test complaint. Both T-Deck images
  must carry the field commands and none of the bench block:

```
for e in t_deck t_deck_plus; do B=<dir>/$e.bin; \
  echo "$e mute=$(strings $B | grep -c 'AUDIO\];mute;') stat=$(strings $B | grep -c 'PERSIST\];stat;') \
  udplog=$(strings $B | grep -c 'UDP\];log') injectraw=$(strings $B | grep -c injectraw)"; done
```

Expected: `mute=2 stat=1 udplog=1 injectraw=0`. On nRF52 scan the `.elf`, not the
ASCII `.hex`.

## Step 6 — Web flasher on GitHub Pages

The browser flasher at https://dk5en.github.io/MeshCom-Firmware/flash/ is the
only path that writes bootloader, partition table and the safeboot factory
partition; the firmware's own OTA writes `ota_0` alone. It is therefore the
delivery route for any release that moves the partition layout or ships a new
safeboot image. Design: `docs/archive/meshcom-web-flasher-plan.md`.

App images come from the release downloaded in Step 5; bootloader, partitions,
otadata and safeboot come from a local build of the same commit (see the
Fallback appendix for the build). Fetch `gh-pages` first, then:

```
git fetch origin gh-pages
uv run --with pytest pytest tools/tests/test_pages_flasher.py
node --test tools/tests/test_flasher_detect.mjs
uv run tools/pages_flasher.py publish --version <tag> --keep 1 --from-release <dir> [--date YYYY-MM-DD] [--push]
uv run tools/pages_flasher.py check --version <tag> --from-release <dir>
```

**Always pass `--keep 1`**: operator policy is that only the latest release is
ever offered in the flasher (older GitHub release objects stay, just not in the
flasher).

`publish` builds a detached `gh-pages` worktree from `origin/gh-pages`, writes
`flash/<tag>/<env>/` for all 30 boards, regenerates `flash/releases.json` and
`flash/detect.json` (hardware ID per board, for the "Board erkennen" button),
prunes release folders beyond `--keep`, and commits. `--date` sets the release
date the flasher shows (default: today). It refuses to run if any artefact is
missing — a partial board never ships. Add `--push` once the commit looks
right; without it the commit stays local. `check` fetches every part over
HTTPS and compares SHA-256 against the local build and the release assets.

Offsets, chip families and the safeboot file per board are derived from the
effective `upload_command` in `platformio.ini` and from `build.mcu` in the
board JSON. Nothing there is hand-maintained, so a new variant needs only its
display name in `DISPLAY` in `tools/pages_flasher.py` (plus the items in the
new-variant checklist below).

**Then refresh the presentation site, in this order — not before the flasher
push above:** `publish` rebuilds its `gh-pages` worktree from
`origin/gh-pages` via `checkout -B gh-pages`, which would discard a
pages-sync commit made first but not yet pushed.

1. Update anything in `docs/presentation/` that names a version or PR history
   (index card, protocol section) for this release.
2. `bash tools/pages-sync.sh -m "docs(pages): sync docs/presentation for <tag>" --push`
   — mirrors `docs/presentation/` onto `gh-pages`, protecting `flash/` (run
   `tools/pages-sync.sh --self-test` if in doubt about that guard).

## Step 7 — Aftermath

- If replacing an earlier release: delete the old release object now
  (`gh release delete <old-tag> -R DK5EN/MeshCom-Firmware --yes`); add
  `--cleanup-tag` ONLY if the user wants the tag gone too. Confirm with
  `gh api repos/DK5EN/MeshCom-Firmware/releases --jq '.[].tag_name'` and
  `git ls-remote --tags origin`.
- Add a dated entry at the top of `docs/RESUME.md` (what shipped, what is
  deliberately still open) and update `docs/BACKLOG.md`; prettier both, commit,
  push.
- Open the flasher, confirm the new version is the default in the version
  dropdown and that the pruned release folder is gone.
- Optionally flash the bench fleet + gateway — that is a separate step; the
  per-board quirks (T-Beam needs esptool at 460800, webflash for
  dk5en-98.local, RAK DFU) live in the flash tooling and auto-memory, not
  here.

## New variant checklist

A new board env needs, besides its `platformio.ini` entry:

- its display name in `DISPLAY` and its env in `RELEASE_ENVS` in
  `tools/pages_flasher.py` (rename in `RELEASE_ASSET_NAME` if CI renames it);
- the `mv ... firmware.bin` rename line and the `artifacts:` entry in
  `.github/workflows/meshcom-ci.yml`, and the hardcoded `27` counts there
  (app images and `.autoupdate` files) plus the 27/66 counts in this file.

## Fallback: local build

Use only when CI is unavailable. The tag push still triggers CI: disable the
`MeshCom CI` workflow first, or expect CI to replace your assets.

- `export MC_BUILD_TAG=<tag>` in the shell that runs the build (detached
  builds need it inside the `bash -c` string); without it the image reports an
  undated version and the reinstall guard cannot tell it from a later
  same-letter release. Changing it rebuilds every env.
- Plain `pio run` builds the 32 `default_envs` (~16 min; never parallel pio
  builds, the cache corrupts; run it with `run_in_background: true` and wait
  for the completion notification, no `sleep` polling). `t5_epaper` and
  `esp32-external-radio` are deliberately not in the release (pre-existing
  breakage; release-notes.md says so).
- Assets as CI ships them, staged in a scratch directory:
  - 27 ESP32 apps: `.pio/build/<env>/firmware.bin` → `<env>.bin`, except
    `LilyGo_T-Beam-1W` → `T-Beam-1W.bin`, `LilyGo_T3_S3_V1_3` →
    `T3_S3_V13.bin`, `LilyGo_T_Connect_Pro` → `t_connect_pro.bin`.
  - 27 `.autoupdate`: `python3 tools/make_autoupdate.py --check <the 27 app .bin>`
    (never support files or nRF52 outputs; the tool refuses inputs without the
    `0xE9` magic).
  - nRF52 (`heltec_t114`, `t_echo`, `wiscore_rak4631`): DFU zip is PlatformIO's
    `.pio/build/<env>/firmware.zip`, renamed `<env>.zip`; UF2 from the hex:
    `python3 ~/.platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py <hex> -c -f 0xADA52840 -o <env>.uf2`.
  - Support files: `safeboot.bin` ← `.pio/build/esp32-safeboot/`,
    `safeboot-s3.bin` ← `.pio/build/esp32-S3-safeboot/`, `bootloader.bin` and
    `partitions.bin` ← `.pio/build/esp32-safeboot/`, `bootloader-s3.bin` ←
    `.pio/build/esp32-S3-safeboot/bootloader.bin`, `otadata.bin` ← repo root.
- Publish: `gh release create <tag> -R DK5EN/MeshCom-Firmware --title "<tag>" --notes-file release-notes.md --latest <staging-dir>/*`,
  then verify as in Step 5.

## Permissions this procedure needs (ask for these up front, don't rediscover mid-run)

Every step below touches a shared or hard-to-reverse resource. None of it is
covered by generic commit/PR permission — confirm explicitly before running
each class of action, even inside an otherwise-approved release:

- **`git push origin fork-dev`** (release docs in Step 3, `docs/RESUME.md` and
  `docs/BACKLOG.md` in Step 7) — pushes to the fork's working branch.
- **`git push origin <tag>`** — pushes a public release tag and thereby
  publishes the Latest release via CI (Step 4). Moving it afterwards is a last
  resort that needs the operator's explicit, on-the-spot consent.
- **`git push` to `gh-pages`** (`pages_flasher.py publish --push`,
  `tools/pages-sync.sh --push`) — before asking, say exactly which live
  flasher release folders this run's `--keep` will prune, or that pages-sync
  is about to overwrite the mirrored site. A denial is a signal about what's
  being removed, not a blanket block on the action.
- **`gh release delete`** (Step 7, and ci-proof cleanup in Step 4) —
  destructive; confirm which of release object vs. tag the user wants gone
  before adding `--cleanup-tag`.
- **Flashing any bench node** — OTA via `tools/webflash.py`, `esptool` for the
  T-Beam, DFU or `pio run -t upload` for the RAK, or a UF2 copy. `pio run` is
  allowlisted and raises no harness prompt by itself, so this permission must
  be asked for explicitly. If a node is mid-soak or mid-capture, flashing it
  ends that run early — confirm which nodes, every time, for every method
  (precedent: 2026-09-28, DK5EN-1 + DK5EN-98 OTA'd mid-soak, ending the 24 h
  XOR soak ~3 h early).
