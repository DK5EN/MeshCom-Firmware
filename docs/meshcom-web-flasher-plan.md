# MeshCom Web Flasher on GitHub Pages: Design and Implementation Plan

Date: 2026-09-18
Status: implemented 2026-09-21 on `fork-neo-test` (waves 1a, 1b, 3). Bench flashes (wave 2)
and the `partitions-16MB.bin` release asset are still open -- see section 12.
Decision: option 1, firmware binaries committed to the `gh-pages` branch and fetched same-origin

## 1. Summary

A browser flasher built on ESP Web Tools, hosted at https://dk5en.github.io/MeshCom-Firmware/flash/,
gives every user a full serial flash of all five flash regions (bootloader, otadata, partition
table, safeboot, app) without installing anything. This is the only way to ship a new safeboot
image or partition layout, because the firmware's OTA path writes the `ota_0` slot only and
nothing in the tree writes the factory partition.

The design copies what https://esptool.oevsv.at/ does, minus its backend: a static page, a
static manifest list, and the binaries next to them on the same origin. The release process
gains one scripted step that copies the built binaries into `gh-pages`, regenerates the
manifests, prunes old releases, and commits.

Effort: about one day including bench flashes on three boards.

## 2. Facts this plan rests on (all verified 2026-09-18)

| Fact                                                                                                                           | How verified                                                   |
| ------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------- |
| Pages site is the `gh-pages` branch of DK5EN/MeshCom-Firmware, HTTPS enforced, 11 files, 1.9 MB                                | `gh api repos/DK5EN/MeshCom-Firmware/pages`, `git ls-tree`     |
| esptool.oevsv.at is ESP Web Tools, self-hosted JS, five parts per board at real offsets                                        | page source, `/api/fwdata` JSON                                |
| GitHub release assets send no `Access-Control-Allow-Origin`, even on a cross-origin GET                                        | `curl` with `Origin: https://dk5en.github.io`                  |
| The running app never writes the safeboot partition; safeboot's updater opens `U_FLASH` only                                   | `src/command_functions.cpp:840`, `src/safeboot/ElegantOTA.cpp` |
| Upload offsets: S3 bootloader 0x0, classic bootloader 0x1000, otadata 0xE000, partitions 0x8000, safeboot 0x10000, app 0xC0000 | `platformio.ini:1082`, `platformio.ini:1086`                   |
| T-Deck and T-Deck Plus use the 16 MB partition table; the release ships only the 4 MB table                                    | `variants/t_deck*/platformio.ini:6`, decoded release asset     |
| nRF52 boards (RAK4631, T114, T-Echo) cannot be flashed via Web Serial; OEVSV offers a download                                 | OEVSV JS, Adafruit UF2 bootloader                              |

## 3. Decision record: why same-origin over a CDN mirror

Option 2 was jsdelivr serving the same `gh-pages` branch with permissive CORS. Rejected for now:

- Adds a third party to the flash path. A CDN outage or policy change breaks the flasher.
- Branch references are cached for hours, so a new release would expose a new manifest pointing
  at stale binaries unless every part is pinned to a commit SHA.
- Buys only bandwidth. A full ESP32 flash is about 2.3 MB. The Pages soft limit of 100 GB per
  month allows roughly 40,000 full flashes per month, far beyond expected use.

The manifest generator takes a base URL parameter. Switching to a CDN later is a one-line change
in the release step, not a redesign.

## 4. Architecture

### 4.1 Layout on `gh-pages`

```
flash/
  index.html            board picker, version picker, install button, nRF52 download fallback
  flasher.js            page logic (no framework)
  esp-web-tools/        vendored ESP Web Tools bundle (install-button.js + chunk files)
  releases.json         list of published releases, newest first
  v4.35t.09.12.2/
    heltec_wifi_lora_32_V3/
      manifest.json
      bootloader.bin
      otadata.bin
      partitions.bin
      safeboot.bin
      firmware.bin
    t_deck/
      ...
    wiscore_rak4631/
      manifest.json     chipFamily NRF52, one part, no offset
      firmware.uf2
  v4.35t.09.11/
    ...
```

Rules:

- One folder per board per release. Every board folder is self-contained, no shared files.
  This matters because the bootloader header carries flash size, mode and frequency, and ESP
  Web Tools writes it as-is, unlike the esptool CLI which patches the header. The per-env
  `bootloader.bin` and `partitions.bin` from `.pio/build/<env>/` are the only correct sources.
  This also fixes the 16 MB partition-table gap for T-Deck by construction.
- Keep the current release plus the previous two. The release step prunes older folders.
  Three releases cost about 150 MB against the 1 GB Pages limit.
- `.nojekyll` stays. Jekyll would otherwise skip nothing here, but it also must not rewrite
  anything.

### 4.2 Manifest per board

ESP32-S3 example (offsets decimal, as ESP Web Tools expects):

```json
{
  "name": "MeshCom Heltec V3",
  "version": "v4.35t.09.12.2",
  "new_install_prompt_erase": true,
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "bootloader.bin", "offset": 0 },
        { "path": "partitions.bin", "offset": 32768 },
        { "path": "otadata.bin", "offset": 57344 },
        { "path": "safeboot.bin", "offset": 65536 },
        { "path": "firmware.bin", "offset": 786432 }
      ]
    }
  ]
}
```

Classic ESP32 differs only in `"chipFamily": "ESP32"` and bootloader offset 4096.

nRF52 boards get `"chipFamily": "NRF52"` with one part and no offset. The page hides the
install button for that family and shows a download link plus the UF2 drag-and-drop steps.

Chip family is derived from the env's `extends` line in `platformio.ini`
(`esp32_s3`, `esp32_classic`, `nrf52_base`), never from a hand-maintained list.

### 4.3 `releases.json`

```json
{
  "releases": [
    {
      "version": "v4.35t.09.12.2",
      "date": "2026-09-12",
      "notes": "https://github.com/DK5EN/MeshCom-Firmware/releases/tag/v4.35t.09.12.2",
      "boards": ["heltec_wifi_lora_32_V3", "t_deck", "wiscore_rak4631"]
    }
  ]
}
```

The page fetches this once and resolves `flash/<version>/<board>/manifest.json` client-side.

### 4.4 Page behaviour

- Board dropdown grouped by vendor with display names (Heltec V3, T-Beam v1.2 SX1262, ...).
  The env-to-display-name table lives in the generator, in one place.
- Version dropdown defaults to the newest release.
- Install button from ESP Web Tools with the manifest URL set per selection.
- Erase prompt on: ESP Web Tools asks the user whether to erase before a full install. The
  default answer stays "keep settings", because NVS holds the callsign and configuration and
  the partition offsets are unchanged between releases.
- A visible notice for unsupported browsers (Safari, Firefox without Web Serial, any mobile).
- A "Migration from official firmware" paragraph: a device on a pre-safeboot partition layout
  must erase, because the partition table moves.

### 4.5 What stays out of scope

- No CI deploy job. Both GitHub Actions workflows are disabled by decision; the release step
  runs on the operator's machine like every other release step.
- No server, no analytics, no external scripts beyond the vendored ESP Web Tools bundle.
- No pre-release channel in the first version. `releases.json` has room for a `channel` field
  later.

## 5. Tooling: `tools/pages_flasher.py`

One script, run from the firmware checkout after step 5 of the release skill:

```
uv run tools/pages_flasher.py publish --version v4.35t.09.13 --keep 3 [--base-url .]
```

Steps it performs:

1. Read the env list and chip family from `pio project config` output.
2. For every ESP32 env: copy `bootloader.bin`, `partitions.bin`, `firmware.bin` from
   `.pio/build/<env>/`, plus `otadata.bin` and the matching `safeboot*.bin` from the repo root,
   into a staging directory. Refuse to run if any file is missing; never ship a partial board.
3. For every nRF52 env: copy the `.uf2` produced in release step 5.
4. Write `manifest.json` per board and update `releases.json`.
5. Check out `gh-pages` into a temporary worktree pinned to `origin/gh-pages`, copy the
   staging tree into `flash/<version>/`, delete release folders beyond `--keep`, and commit
   with explicit paths. Push only when `--push` is given.
6. Print the resulting URL and the total size of `flash/`.

Also: `tools/pages_flasher.py check --version <v>` fetches every part over HTTPS from the live
site and compares SHA-256 against the local build. This is the positive existence check the
release skill requires.

Regression tests (`test/native` or a plain pytest under `tools/`): manifest generation for one
S3 env, one classic env, one nRF52 env, and the prune logic. The 16 MB partition table must
appear in the T-Deck manifest folder and the 4 MB one in the Heltec folder.

## 6. Release process integration

Add to the release-firmware skill after step 5 (assemble assets) and before step 6 (publish):

- Step 5b: `pages_flasher.py publish` on the tagged build, then `pages_flasher.py check`.
- Step 7 (aftermath): confirm the flasher offers the new version and that the pruned folder
  is gone.

The GitHub release keeps its current 39 assets. The flasher is additive. The release notes get
a one-line link to the flasher.

Fix in the same change: ship `partitions-16MB.bin` as a release asset next to the existing
`partitions.bin`, because the current asset set cannot fully flash a T-Deck.

## 7. Implementation waves

Read-only scouting is done. Writers are dispatched per orchestrate-waves with disjoint files.

| Wave | Task                                                                                                                 | Owner        | Files                                                                            |
| ---- | -------------------------------------------------------------------------------------------------------------------- | ------------ | -------------------------------------------------------------------------------- |
| 1a   | Generator script plus tests                                                                                          | implementer  | `tools/pages_flasher.py`, `tools/tests/test_pages_flasher.py`                    |
| 1b   | Static page, vendored ESP Web Tools bundle, display-name table                                                       | implementer  | `pages/flash/index.html`, `pages/flash/flasher.js`, `pages/flash/esp-web-tools/` |
| gate | Run generator against a full local build, publish to a staging folder `flash-staging/` on `gh-pages`, open in Chrome | orchestrator | `gh-pages` commit                                                                |
| 2    | Bench flashes, serialized: Heltec V3 (S3, 8 MB), T-Beam v1.2 (classic), T-Deck (S3, 16 MB)                           | orchestrator | none                                                                             |
| 3    | Release skill step 5b, release notes line, `partitions-16MB.bin` asset                                               | implementer  | `~/.claude/skills/release-firmware/SKILL.md`, `docs/release.md`                  |
| gate | Advisor pass on the generator and page, then move `flash-staging/` to `flash/`                                       | orchestrator | `gh-pages` commit                                                                |

The page sources live under `pages/flash/` on the working branch so they are reviewed like
code. The generator copies them to `gh-pages` on publish. The current `gh-pages` content
(index.html, the three HTML docs, assets) is not touched.

## 8. Bench verification

Each board, in this order, all on the fork bench fleet:

1. `--info` before: note build time and callsign.
2. Flash via the staging page in Chrome, "keep settings".
3. `--info` after: new build time, same callsign, node boots into the app, not into safeboot.
4. Trigger the safeboot path once (`--update` style command that reboots into safeboot) and
   confirm the safeboot status page reports the version shipped in the flash. This is the
   proof that the factory partition was actually rewritten.
5. T-Deck only: confirm SPIFFS mounts, which proves the 16 MB table went in.
6. One erase-then-flash run on the Heltec to confirm the migration path from an unknown
   layout.

Known bench pitfalls apply: use `--upload-port` style explicit ports, S3 native USB needs DTR,
T-Deck reboots on port open.

## 9. Risks and mitigations

| Risk                                                | Mitigation                                                                  |
| --------------------------------------------------- | --------------------------------------------------------------------------- |
| Bootloader header mismatch bricks boot on one board | Per-env bootloader from the build tree, bench flash on all three chip cases |
| A user flashes the wrong board                      | Display names with photos later; first version shows the env name and chip  |
| `gh-pages` grows without bound                      | `--keep 3` prune in the publish step, size printed on every run             |
| Pages bandwidth                                     | Math in section 3; revisit if the monthly Pages traffic warning appears     |
| ESP Web Tools bundle update changes behaviour       | Vendored, version pinned in a `VERSION` file, updated deliberately          |
| User on Safari or a phone                           | Detect `navigator.serial`, show the download fallback and browser note      |
| Erase prompt destroys a user's callsign             | Default answer keeps settings; text explains when erase is needed           |

## 10. Acceptance criteria

- Heltec V3, T-Beam v1.2 and T-Deck flashed from the live page boot into the app and report the
  new build in `--info`.
- Safeboot status page on each shows the safeboot version shipped with the release.
- `pages_flasher.py check` passes against the live site.
- `gh-pages` holds exactly three release folders after two consecutive publishes with `--keep 3`.
- RAK4631 entry offers the `.uf2` download and shows the UF2 instructions.
- Release skill documents step 5b, and a dry run of the skill text reaches the flasher step
  without manual detours.

## 11. Open questions for the operator

- Display names and grouping for the board dropdown: mirror the OEVSV grouping, or the fork's
  env names with a short description? Plan assumes OEVSV-style grouping.
- Should the flasher also offer official icssw-org releases? Their assets have the same CORS
  problem, so it would mean mirroring their binaries into `gh-pages` too. Plan assumes fork
  releases only.

## 12. Implementation record (2026-09-21)

Built on `fork-neo-test`, published as `v4.35t.09.21-neo`. What deviates from sections 4-7:

| Plan                                                     | As built                                                                                                                                                                                     |
| -------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Chip family from the env's `extends` line                | From `build.mcu` in the board JSON. `extends` is not a reliable discriminator: five envs carry their own `upload_command` and none of the three base sections                                |
| Offsets from a table in the generator                    | Parsed out of the env's effective `upload_command`, i.e. out of the exact esptool call the project itself uses. `t_deck_pro` has its line commented out and falls back to the family default |
| `manifest.json` parts in `upload_command` order          | Sorted ascending by offset, so the manifest reads like the flash map                                                                                                                         |
| Safeboot part published as `safeboot.bin` on every board | Published under its real name, `safeboot.bin` or `safeboot-s3.bin`. Board folders are self-contained, so the name may differ per folder                                                      |
| `releases.json` lists board envs as bare strings         | Lists `{env, group, name, chipFamily}`, so the page needs no vendor or display-name knowledge of its own                                                                                     |
| `flash-staging/` gate before `flash/`                    | Skipped by operator decision; published straight into `flash/`                                                                                                                               |

### Wave 2, bench (2026-09-21 evening)

Two boards flashed from the live page and verified by reading the flash back over esptool and
comparing SHA-256 against the shipped artefacts. This is stronger than section 8 asked for: it
proves the factory partition directly instead of inferring it from the safeboot status page.

| Board                                   | Regions read back                        | Result             |
| --------------------------------------- | ---------------------------------------- | ------------------ |
| Heltec V3 (ESP32-S3, 4 MB, erase first) | 0x0, 0x8000, 0xE000, 0x10000, 0xC0000    | all five identical |
| T-Beam (classic ESP32, erase first)     | 0x1000, 0x8000, 0xE000, 0x10000, 0xC0000 | all five identical |

Both boot into the app, not into safeboot, and report the build stamp of the shipped image.
After the erase the callsign is the factory `XX0XXX-00` and the node refuses to transmit
(`[TX];refuse;unconfigured`), which is correct. The T-Beam covers the classic-ESP32 bootloader
at 0x1000, the one offset the Heltec cannot exercise.

**Finding, fixed the same evening:** the board dropdown named the three T-Beam entries after a
board revision ("T-Beam v1.1 (SX1276)", "T-Beam v1.2 (SX1262)"). The envs share one board
definition and differ only in the radio chip, so the labels invented a distinction the code does
not make. A v1.2 board carrying an SX1276 got the "v1.2" entry and came up with
`SX1262 chip Initializing ... failed, code -2`. The entries now name the chip alone, and the page
says so under the board picker. Section 9's "user flashes the wrong board" risk was real, and the
mitigation was made worse by the display table, not by the mechanism.

Still open:

- Wave 2 for the T-Deck: the 16 MB partition table and the SPIFFS mount are unproven on hardware.
- Section 6's `partitions-16MB.bin` release asset. The flasher no longer needs it -- each board
  folder carries its own table -- but the manual asset set still cannot fully flash a T-Deck.
  Adding it moves the release from 39 to 40 assets and breaks the diff-identical name check in
  release step 5, so it is a deliberate separate change.
