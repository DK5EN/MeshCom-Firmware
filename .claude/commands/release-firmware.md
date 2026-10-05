---
description: Cut and publish a complete MeshCom fork release - docs, gates, tag, all 32 build envs, 39 GitHub assets, web flasher on GitHub Pages - without rediscovering the process
allowed-tools: [Bash, Read, Edit, Write, Glob, Grep]
---

Cut a full stability release of this fork and publish it on GitHub. Both
GitHub Actions workflows are disabled — a tag push builds and publishes
nothing; every step below is manual and this procedure is the only publishing
path.

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
  2026-10-02: `upstream/dev` plus one fork port commit; `fork-neo-test` is
  retired as tag `archive/fork-neo-test-20261002`, `fork-main` as
  `archive/fork-main-20260927`). Older docs that say `fork-neo-test`,
  `fork-main` or `v4.35p_prio` describe retired lines. A fork release is the
  fork-dev build, which carries a few firmware fixes upstream does not have
  yet, so it is NOT byte-identical to the official upstream release of the
  same version -- say so in release-notes.md.

## Versioning

- Scheme: `v<VER>.MM.DD` (no suffix -- the `-stability` suffix was dropped
  on 2026-09-05; every tag up to and including `v4.35s.09.03-stability`
  carries it, nothing newer does), where `<VER>` is the firmware version the
  tree currently carries -- read it from `SOURCE_VERSION` +
  `SOURCE_VERSION_SUB` in `src/configuration_global.h`, never from an older
  tag. The letter follows upstream and changes without notice (4.35p ->
  4.35s on 2026-09-03). A second cut on the same day appends `.2`:
  `v<VER>.MM.DD.2` (precedents: v4.35p.08.27.2-stability, v4.35p.07.24.2).
  Ask the user for the tag name if there is any ambiguity (same-day
  re-release vs. replace-in-place).
- **Check upstream's tags before naming ours**: `git ls-remote --tags upstream`.
  Upstream uses the same `v<VER>.MM.DD` shape since 2026-09-12 (`v4.35t.09.12`
  on their `dev` merge of our PR #1140), and since 4.40a the bare `v<VER>`
  (`v4.40a` = their merge of our PR #1186). A fork tag with an upstream tag's
  name clobbers on every fetch, so never reuse a bare upstream tag: use
  `v<VER>.MM.DD` (precedent: `v4.40a.10.02`) or append `.2` and say why in
  release-notes.md (precedent: `v4.35t.09.12.2`). The `-neo` suffix is retired. Also `git fetch upstream --tags` first so the
  upstream name exists locally and nothing is pushed under it later.
- The letter no longer marks the fork: upstream released official `v4.35t` on
  2026-09-10 with our PR #1135 (items 104-210) inside. Release text must frame
  the delta against the official release that actually exists (check
  `gh release list -R icssw-org/MeshCom-Firmware`), and point at the flash
  stamp in `--info` as the distinguishing mark.
- The GitHub release title is the bare tag name. The release-notes headings
  and the `## New in <tag>` changelog sections use the bare tag too.
- `FLASH_VERSION` in `src/configuration_global.h`: bump to the release date
  (`YYYYMMDD`) for a new-day release; for a same-day `.2` cut it **stays**
  (08.27.2 precedent). It is purely informative since 3a31317b.
  `FLASH_STRUCT_VERSION` moves ONLY on a real settings-layout change — it
  wipes fleet configs, never touch it casually.

## Step 1 — Release documents

Two files are updated for every release; a topic changelog only when the
content fits one:

- `release-notes.md` (repo root, **English**) — the GitHub release body and
  nothing else; only the current release. Keep the structure: `[!IMPORTANT]`
  not-official box, "What this release is", changelog/PR-draft links (they
  embed the tag name — update to the new tag!), "What changes on the air",
  "Supported Hardware" split *bench-tested* vs *built and shipped, not on
  our bench*, "Known gaps, stated plainly" (list every open defect honestly,
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

## Step 2 — Gates (all must be green before tagging)

Gate = every `[env:native*]` env in `platformio.ini`, enumerated at run time
(the list grows; do not hardcode it), each run individually — **never** bare
`pio test`, it walks every environment including board envs and flashes
whatever hardware is attached — plus the golden selftest:

```
for e in $(grep -oE '^\[env:native[^]]*\]' platformio.ini | tr -d '[]' | sed 's/^env://'); do
  pio test -e "$e" || { echo "FAILED: $e" >&2; exit 1; }
done
bash test/golden/selftest.sh
```

Copy the total case count this run actually reports into release-notes.md —
never a count remembered from an earlier release.

## Step 3 — Build all 32 release environments, then verify

**AU:** `export MC_BUILD_TAG=<tag>` first (see Step 5a, item 1).

Build and verify BEFORE tagging, so the tag always matches what these
commands actually produce. Tagging first and building after is what let a
release ship with stale safeboot bins and then need its tag moved to fix it —
see the safeboot check below.

Sequential, one `pio run` invocation (~16 min). Never run parallel pio
builds of the same env — the build cache corrupts. The Bash tool's background
mode still enforces its 10-minute timeout, so launch the build detached
(`nohup bash -c 'pio run ...; echo "exit=$?"' > build.log 2>&1 & disown`) and
wait with a backgrounded `until grep -q '^exit=' build.log` loop; re-arm the
waiter if it times out. pio processes envs in `platformio.ini` order, not
command-line order.

```
pio run -e E22-DevKitC -e E22_1262-DevKitC -e E22_1262_S3-DevKitC-1-N16R8 \
  -e E22_1268_S3-DevKitC-1-N16R8 -e E22_XML-DevKitC -e esp32-loraprs-e22 \
  -e esp32-loraprs-ra01 -e heltec_wifi_lora_32_V2 -e heltec_wifi_lora_32_V3 \
  -e heltec_wifi_lora_32_V4 -e heltec_wireless_stick -e heltec_wireless_tracker \
  -e LilyGo_T-Beam-1W -e T-ETH-ELITE_1262 -e LilyGo_T3_S3_V1_3 \
  -e ttgo-lora32-v21 -e ttgo_tbeam -e ttgo_tbeam_supreme -e ttgo_tbeam_SX1262 \
  -e ttgo_tbeam_SX1268 -e LilyGo_T_Connect_Pro -e t_deck -e t_deck_plus \
  -e t_deck_pro -e vision-master-e213 -e vision-master-e290 -e wireless-paper \
  -e heltec_t114 -e t_echo -e wiscore_rak4631 -e esp32-safeboot -e esp32-S3-safeboot
```

`t5_epaper` is deliberately NOT in the release (pre-existing include-path
breakage; release-notes.md says so).

Do NOT skip this step because "the envs were all built earlier this
session": `.pio` outputs disappear between runs (observed 2026-08-31 —
7 of 27 app images and both safeboot outputs gone within the hour, despite
an exit-0 full build). If you think the outputs are current, the check is
`ls .pio/build/*/firmware.bin | wc -l` == 27 plus the three nRF52 hex files
plus both safeboot outputs — anything less means rebuild.

**Safeboot check after the build:** the safeboot envs' post script
(`tools/safeboot.py`) copies `safeboot.bin`/`safeboot-s3.bin` into the repo
root, where they are **tracked in git**. ESP32 app builds are NOT
byte-reproducible (two clean builds of identical source differ in ~10% of
bytes, a pure layout shift) — but for the safeboot images the only field this
touches is one fixed header: `git status` showing them modified after every
build is expected, not proof of staleness. Diff the new bins against the
tracked copies: a difference confined to the `esp_app_desc` `app_elf_sha256`
field (bytes 177-208) plus the trailing digest that covers it (64-65 bytes
total) is that expected header-only noise — either discard it (`git checkout
-- safeboot.bin safeboot-s3.bin`) or commit it deliberately in Step 4; either
is fine as long as it's settled here, before tagging. A difference outside
that range, or a size change, is real staleness — stop and investigate before
Step 4.

**Field-command string scan after the build (INS-01/INS-04):** a compile-guard
change can drop shipped commands without a compiler or test complaint. Both
T-Deck images must carry the field commands and none of the bench block:

```
for e in t_deck t_deck_plus; do B=.pio/build/$e/firmware.bin; \
  echo "$e mute=$(strings $B | grep -c 'AUDIO\];mute;') stat=$(strings $B | grep -c 'PERSIST\];stat;') \
  udplog=$(strings $B | grep -c 'UDP\];log') injectraw=$(strings $B | grep -c injectraw)"; done
```

Expected: `mute=2 stat=1 udplog=1 injectraw=0`. On nRF52 scan the `.elf`, not
the ASCII `.hex`.

## Step 4 — Commit docs + safeboot bins, tag, push

Commit the Step 1 doc changes together with whatever Step 3's safeboot check
decided (a refresh commit if the bins were kept, nothing extra if they were
discarded), push, then tag:

```
git tag -a <tag> -m "<one-line summary>"
git push origin <tag>
```

Create and push the tag only after Step 3's build and safeboot check already
match the working tree — never before. Moving an already-pushed tag
(`git tag -d`, `git push origin :refs/tags/<tag>`, re-tag, re-push) is a last
resort for a real problem found after the push, not a standard part of this
procedure, and needs the operator's explicit, on-the-spot consent every time.

## Step 5 — Assemble the 66 assets (39 + 27 AU `.bin.zz`)

Stage in a scratch directory. Exact recipe (verified byte-for-byte-in-name
against 08.28/08.31/08.31.2):

- **27 ESP32 app images**: `.pio/build/<env>/firmware.bin` → `<env>.bin`,
  EXCEPT three renames:
  - `LilyGo_T-Beam-1W` → `T-Beam-1W.bin`
  - `LilyGo_T3_S3_V1_3` → `T3_S3_V13.bin`
  - `LilyGo_T_Connect_Pro` → `t_connect_pro.bin`
- **3 nRF52 boards** (`heltec_t114`, `t_echo`, `wiscore_rak4631`), each as
  `.uf2` AND DFU `.zip`, both from `.pio/build/<env>/firmware.hex`:
  - UF2: `python3 ~/.platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py <hex> -c -f 0xADA52840 -o <env>.uf2`
  - DFU zip: `python3 ~/.platformio/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py dfu genpkg --dev-type 0x0052 --sd-req 0x00B6 --application <hex> <env>.zip`
    (do NOT use the binary in `framework-arduinoadafruitnrf52/tools/adafruit-nrfutil/macos/` — it ships non-executable)
- **6 support files**:
  - `bootloader.bin` ← `.pio/build/esp32-safeboot/bootloader.bin` (classic ESP32)
  - `bootloader-s3.bin` ← `.pio/build/esp32-S3-safeboot/bootloader.bin`
  - `partitions.bin` ← `.pio/build/E22-DevKitC/partitions.bin` (shared 4MB-safeboot layout; identical across the classic-ESP32 envs)
  - `otadata.bin`, `safeboot.bin`, `safeboot-s3.bin` ← repo root (tracked)

**Verify before publishing** — the name list must be diff-identical to the
previous release:

```
diff <(gh release view <prev-tag> -R DK5EN/MeshCom-Firmware --json assets \
  --jq '.assets[].name' | sort) <(ls <staging-dir> | sort)
```

(If the previous release is already gone, the canonical 39-name list is in
this file's history and in docs/RESUME.md 2026-08-31.)

### Step 5a — Auto Update assets (AU, since 2026-10-05)

Since AU (#1187) every release carries a compressed twin of each ESP32 app image
(AU-D11) and every build embeds its release tag (AU-D16). Two additions to the flow
above, plus a check.

**1. Export `MC_BUILD_TAG` before the Step 3 builds.** Use the tag exactly as it
will be pushed (`v<VER>.MM.DD[.N]`, for example `v4.40a.10.02`), in the same shell
that runs `pio run`:

```
export MC_BUILD_TAG=<tag>
pio run -e ... (the full env list)
```

- `tools/mc_build_defines.py` turns it into the `MC_BUILD_TAG` macro. Without it the
  image reports an undated version and the reinstall guard (`fwShouldInstall`) cannot
  tell it from any later same-letter release.
- Changing `MC_BUILD_TAG` changes the compiler flags of every translation unit, so
  PlatformIO rebuilds every env completely. Budget the full ~16 min and never
  change the tag between envs of one release.
- Detached builds (`nohup bash -c ...`) inherit the variable only if it is exported
  first; put `export MC_BUILD_TAG=<tag>` inside the `bash -c` string.
- Check one image afterwards: `strings .pio/build/heltec_wifi_lora_32_V3/firmware.bin | grep -F '<tag>'`.

**2. Compress the 27 ESP32 app images in Step 5.** Run it in the staging directory
on the already renamed `.bin` files, so the `.zz` names follow the `.bin` names
(`<env-or-renamed>.bin.zz`: `T-Beam-1W.bin.zz`, `T3_S3_V13.bin.zz`,
`t_connect_pro.bin.zz`, ...). Only the 27 app images, never the support files
(`bootloader*.bin`, `partitions.bin`, `otadata.bin`, `safeboot*.bin`) and never the
nRF52 outputs:

```
python3 tools/make_zz.py --check --manifest au-zz-manifest.json \
  $(ls <staging-dir>/*.bin | grep -v -E '/(bootloader|bootloader-s3|partitions|otadata|safeboot|safeboot-s3)\.bin$')
```

The tool refuses any input whose first byte is not `0xE9`, so a support file in the
list fails loudly instead of being compressed. `--check` re-inflates every output
and compares it byte-exact with its input. Keep `au-zz-manifest.json` out of the
upload; it is for the release notes (sizes, ratios, hashes).

**3. Upload the `.bin.zz` assets next to the `.bin` ones.** Same `gh release
create` / `gh release upload` call as the other assets; the asset name is the
`.bin` name plus `.zz`. The release now has 39 + 27 = 66 assets (update the count
in the Step 7 verify).

**4. Verify before publishing.**

```
ls <staging-dir>/*.bin.zz | wc -l          # must equal the number of ESP32 app .bin (27)
python3 tools/make_zz.py --check <staging-dir>/<app>.bin ...   # again, exit status 0
```

- The count of `.bin.zz` must equal the count of ESP32 app `.bin` files (27); the
  name list stays diff-identical to the previous release apart from the 27 new
  `.zz` names.
- Every `.zz` begins with the bytes `78 da` (`xxd -l2 <file>.zz`).
- Compressed size is about 64 % of the image (Heltec V3: 1,560,864 B to 993,337 B), so
  expect each `.zz` well below its `.bin`; a `.zz` as large as its `.bin` means the
  input was already compressed or wrong.

## Step 5b — Web flasher on GitHub Pages

The browser flasher at https://dk5en.github.io/MeshCom-Firmware/flash/ is the
only path that writes bootloader, partition table and the safeboot factory
partition; the firmware's own OTA writes `ota_0` alone. It is therefore the
delivery route for any release that moves the partition layout or ships a new
safeboot image. Design: `docs/meshcom-web-flasher-plan.md`.

Runs against the same `.pio/build` tree step 3 produced, so it goes after
step 5 and before the GitHub release:

```
uv run --with pytest pytest tools/tests/test_pages_flasher.py
node --test tools/tests/test_flasher_detect.mjs
uv run tools/pages_flasher.py publish --version <tag> --keep 1
```

**Always pass `--keep 1` explicitly, even though the tool's own default is now
1 too** (changed from 3). Operator policy: only the latest release is ever
offered in the flasher, so nobody picks a wrong one (older GitHub release
objects stay, just not in the flasher). This skill used to show `--keep 3` as
an example here while the tool still defaulted to 3, and that example was
followed literally on 2026-09-28, leaving two versions live in the flasher
until caught and corrected — name `--keep 1` regardless of what the tool
currently defaults to.

`publish` builds a detached `gh-pages` worktree from `origin/gh-pages`, writes
`flash/<tag>/<env>/` for all 30 boards, regenerates `flash/releases.json` and
`flash/detect.json` (hardware ID per board, for the "Board erkennen" button),
prunes release folders beyond `--keep`, and commits. `--date YYYY-MM-DD`
sets the release date the flasher shows (default: today) -- use it when the
release is dated ahead of the build day. It refuses to run if any
artefact is missing — a partial board never ships. Add `--push` once the
commit looks right; without it the commit stays local and only the local
`gh-pages` branch is moved.

Offsets, chip families and the safeboot file per board are derived from the
effective `upload_command` in `platformio.ini` and from `build.mcu` in the
board JSON. Nothing there is hand-maintained, so a new variant needs only its
display name in `DISPLAY` in `tools/pages_flasher.py`.

After the push, the positive existence check:

```
uv run tools/pages_flasher.py check --version <tag>
```

It fetches every part over HTTPS and compares SHA-256 against the local build.

**Then refresh the presentation site, in this order — not before the flasher
push above:** `publish` rebuilds its `gh-pages` worktree from
`origin/gh-pages` via `checkout -B gh-pages`, which would discard a
pages-sync commit made first but not yet pushed.

1. Update anything in `docs/presentation/` that names a version or PR history
   (index card, protocol section) for this release.
2. `bash tools/pages-sync.sh -m "docs(pages): sync docs/presentation for <tag>" --push`
   — mirrors `docs/presentation/` onto `gh-pages`, protecting `flash/` (run
   `tools/pages-sync.sh --self-test` if in doubt about that guard).

## Step 6 — Publish

```
gh release create <tag> -R DK5EN/MeshCom-Firmware --title "<tag>" \
  --notes-file release-notes.md --latest <staging-dir>/*
```

Verify: `gh release view <tag> -R DK5EN/MeshCom-Firmware --json assets,isDraft --jq '{assets:(.assets|length),isDraft}'`
must show 66 assets (39 + 27 `.bin.zz`), not draft. (`isLatest` is not a valid `--json` field.)

## Step 7 — Aftermath

- If replacing an earlier release: delete the old release object now
  (`gh release delete <old-tag> -R DK5EN/MeshCom-Firmware --yes`); add
  `--cleanup-tag` ONLY if the user wants the tag gone too. Confirm with
  `gh api repos/DK5EN/MeshCom-Firmware/releases --jq '.[].tag_name'` and
  `git ls-remote --tags origin`.
- Add a dated entry at the top of `docs/RESUME.md` (what shipped, what is
  deliberately still open), prettier it, commit, push.
- Open the flasher, confirm the new version is the default in the version
  dropdown and that the pruned release folder is gone.
- Optionally flash the bench fleet + gateway — that is a separate step; the
  per-board quirks (T-Beam needs esptool at 460800, webflash for
  dk5en-98.local, RAK DFU) live in the flash tooling and auto-memory, not
  here.

## Permissions this procedure needs (ask for these up front, don't rediscover mid-run)

Every step below touches a shared or hard-to-reverse resource. None of it is
covered by generic commit/PR permission — confirm explicitly before running
each class of action, even inside an otherwise-approved release:

- **`git push origin <branch>`** (the release-doc commit in Step 1/4, the
  safeboot-bin refresh folded into that same Step 4 commit, `docs/RESUME.md`
  in Step 7) — pushes to the fork's default branch.
- **`git push origin <tag>`** — pushes a public release tag. Step 4 pushes it
  only after Step 3's build and safeboot check already match the tree, so it
  should never need to move afterward. If a real problem is still found once
  it's pushed, moving it (`git tag -d`, `git push origin :refs/tags/<tag>`,
  re-tag, re-push) is a last resort, not a standard step of this procedure —
  ask for the operator's explicit, on-the-spot consent before doing it.
- **`git push` to `gh-pages`** (`pages_flasher.py publish --push`,
  `tools/pages-sync.sh --push`) — before asking, say exactly which live
  flasher release folders this run's `--keep` will prune, or that pages-sync
  is about to overwrite the mirrored site. A denial is a signal about what's
  being removed, not a blanket block on the action: one 2026-09-28 run was
  denied by the auto-mode classifier because it pruned a live release nobody
  had asked to remove, while three other gh-pages pushes that same day went
  through without incident.
- **`gh release create`** — publishes a public GitHub release with 66 assets (incl. 27 AU `.bin.zz`).
- **`gh release delete`** (Step 7, only when replacing an earlier release) —
  destructive; confirm which of release object vs. tag the user wants gone
  before adding `--cleanup-tag`.
- **Flashing any bench node** — OTA via `tools/webflash.py`, `esptool` for the
  T-Beam, DFU or `pio run -t upload` for the RAK, or a UF2 copy (the optional
  last bullet of Step 7, and the per-board methods it names). `pio run` is
  allowlisted and raises no harness prompt by itself, so this permission must
  be asked for explicitly, not inferred from the absence of a prompt. If a
  node is mid-soak or mid-capture, flashing it ends that run early — confirm
  which nodes and that ending any running test on them is fine, every time,
  for every flash method (precedent: 2026-09-28 14:33, DK5EN-1 + DK5EN-98
  OTA'd mid-soak with the user's explicit go-ahead, ending the 24 h XOR soak
  ~3 h early at a ~21 h actual window; logged in `docs/RESUME.md` and
  auto-memory `soak-xor-20260927`).

## Honesty rules for the release text

- Only claim bench verification for boards that actually had bench time this
  cycle; everything else goes under "built and shipped, not on our bench".
- Every known-but-unfixed defect goes into "Known gaps, stated plainly" —
  releasing with an open defect is the user's call, hiding it is not.
- The native-test count in release-notes.md must be copied from Step 2's
  actual output for this run, never carried over from an earlier release. If
  a topic changelog was touched in Step 1, its numbering/section title in
  release-notes.md must match what that file actually contains now.
- Release-notes.md's account of "what changed" must cover every commit Step 1's
  `git log <prev-tag>..HEAD -- src variants platformio.ini` lists — a scope
  claim not checked against that range is not trustworthy (the 09.28.2 notes
  said "a single fix" but the tag also shipped `dbc57632`).
