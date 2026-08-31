---
description: Cut and publish a complete MeshCom fork release - docs, gates, tag, all 32 build envs, 39 GitHub assets - without rediscovering the process
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
  branches for a release.

## Versioning

- Scheme: `v4.35p.MM.DD-stability` (fork stability line). A second cut on
  the same day appends `.2`: `v4.35p.MM.DD.2-stability` (precedents:
  v4.35p.08.27.2-stability, v4.35p.07.24.2). Ask the user for the tag name
  if there is any ambiguity (same-day re-release vs. replace-in-place).
- `FLASH_VERSION` in `src/configuration_global.h`: bump to the release date
  (`YYYYMMDD`) for a new-day release; for a same-day `.2` cut it **stays**
  (08.27.2 precedent). It is purely informative since 3a31317b.
  `FLASH_STRUCT_VERSION` moves ONLY on a real settings-layout change — it
  wipes fleet configs, never touch it casually.

## Step 1 — Release documents (three files, distinct jobs)

- `release-notes.md` (repo root, **English**) — the GitHub release body and
  nothing else; only the current release. Keep the structure: `[!IMPORTANT]`
  not-official box, "What this release is", changelog/PR-draft links (they
  embed the tag name — update to the new tag!), "What changes on the air",
  "Supported Hardware" split *bench-tested* vs *built and shipped, not on
  our bench*, "Known gaps, stated plainly" (list every open defect honestly,
  e.g. TM-49), "Installing", "Upstream". The README link also embeds the tag.
- `release.md` (repo root, **German, no umlauts** — write `ae/oe/ue`, `--`
  for em dashes) — running journal. New section at the TOP, below the header
  that names FLASH_VERSION. Must end with "Was fuer dieses Release auf
  Hardware geprueft wurde" and "Was ausdruecklich NICHT geprueft wurde".
- `docs/CHANGELOG-stability.md` (**English**) — one continuously numbered
  list. New `## New in <tag>` section ABOVE the previous one; numbering
  continues from the last item (08.31.2 ended at 156).

Then: `npx --yes prettier@3 --write release-notes.md release.md docs/CHANGELOG-stability.md`
(a `.prettierignore` already protects binaries).

## Step 2 — Gates (all must be green before tagging)

Full native suite, 12 host envs (baseline 445 cases as of 08.31.2 — the
count in release-notes.md must match reality):

```
pio test -e native -e native_aprs -e native_parsers -e native_batt_detect \
  -e native_conf_frame -e native_extern -e native_config -e native_xml \
  -e native_aprs_fuzz -e native_capture -e native_dedup -e native_extradio
```

## Step 3 — Commit docs, tag, push

Commit the doc changes on the working branch, push, then:

```
git tag -a <tag> -m "<one-line summary>"
git push origin <tag>
```

## Step 4 — Build all 32 release environments

Sequential, one `pio run` invocation, run in background (~16 min). Never run
parallel pio builds of the same env — the build cache corrupts.

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

**Safeboot check after the build:** the safeboot envs' post script
(`tools/safeboot.py`) copies `safeboot.bin`/`safeboot-s3.bin` into the repo
root, where they are **tracked in git**. The build is deterministic — if
`git status` shows them modified (or `md5 -q` differs from the tracked
files), the tracked bins were stale: commit them and move the tag BEFORE
publishing, so tag content and shipped assets match.

## Step 5 — Assemble the 39 assets

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

## Step 6 — Publish

```
gh release create <tag> -R DK5EN/MeshCom-Firmware --title "<tag>" \
  --notes-file release-notes.md --latest <staging-dir>/*
```

Verify: `gh release view <tag> -R DK5EN/MeshCom-Firmware --json assets,isDraft --jq '{assets:(.assets|length),isDraft}'`
must show 39 assets, not draft. (`isLatest` is not a valid `--json` field.)

## Step 7 — Aftermath

- If replacing an earlier release: delete the old release object now
  (`gh release delete <old-tag> -R DK5EN/MeshCom-Firmware --yes`); add
  `--cleanup-tag` ONLY if the user wants the tag gone too. Confirm with
  `gh api repos/DK5EN/MeshCom-Firmware/releases --jq '.[].tag_name'` and
  `git ls-remote --tags origin`.
- Add a dated entry at the top of `docs/RESUME.md` (what shipped, what is
  deliberately still open), prettier it, commit, push.
- Optionally flash the bench fleet + gateway — that is a separate step; the
  per-board quirks (T-Beam needs esptool at 460800, webflash for
  dk5en-98.local, RAK DFU) live in the flash tooling and auto-memory, not
  here.

## Honesty rules for the release text

- Only claim bench verification for boards that actually had bench time this
  cycle; everything else goes under "built and shipped, not on our bench".
- Every known-but-unfixed defect goes into "Known gaps, stated plainly" —
  releasing with an open defect is the user's call, hiding it is not.
- The native-test count and item numbers in release-notes.md must match the
  actual gate output and CHANGELOG numbering of THIS release.
