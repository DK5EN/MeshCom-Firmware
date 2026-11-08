# Backlog - MeshCom Firmware Hardening Campaign

Working document for picking the campaign back up: what we set out to do, how we decided to get
there, what is still open. The dated campaign journal (2026-08-18 to 2026-09-29) lives in
[`archive/backlog-journal-20260818-20260929.md`](archive/backlog-journal-20260818-20260929.md).

## Stand 2026-11-07

- **Branch model:** `fork-dev` is the only working branch: `upstream/dev` (icssw-org) plus the
  fork's docs, tests, tools, GitHub Pages and fork-only firmware not upstream yet. `fork-main`,
  `fork-neo`, `fork-neo-test` and `master` are retired (tag `archive/fork-neo-test-20261002`).
- **Upstream:** `upstream/dev` 649b5b17 is merged into `fork-dev` (`71c324e7`). Our PRs #1238, #1243-#1245, #1247-#1251,
  #1257 are merged; no PR of ours is open. DK5EN never merges upstream himself.
- **Last release:** `v4.40a.11.06` (2026-11-06, Latest, tag on `3ecf429e`, built by CI, run 42306803386). 68 assets.
  Unreleased on `fork-dev`: GPIO-01..05, GPSV4-01 W1, TD-THEME-01 W1-W3. Release history:
  [`release-journal.md`](release-journal.md).
- **Gate:** `tools/regression.sh --stage 1,2` (host) or `--stage all` (plus bench). Last full run: 101 envs, 3602 Unity
  cases, 1500 pytest (release gate of `v4.40a.11.06`). Inventory: [`test-suite-map.md`](test-suite-map.md).
- **Open work:** section 3.2, the release train first, then one short table per kind (implement, bench, soak,
  field, waiting, upstream, studies, ideas). Closed rows move to the archive: since 2026-11-07
  [`archive/backlog-rows-20261107.md`](archive/backlog-rows-20261107.md) (also the full text of every open row),
  before that [`archive/backlog-closed-20261024.md`](archive/backlog-closed-20261024.md).
- **Journal:** the 7000-line campaign log (sections 3.8a to 3.8bb, 3.9, the Durchgang entries,
  old branch tables) moved to the archive file above, headings unchanged. Citations of the form
  "BACKLOG.md 3.8x" resolve there. Hand-over: [`RESUME.md`](RESUME.md). Release detail:
  [`release-journal.md`](release-journal.md). Older campaign papers (NTP/TZ/RTC, concept
  2026-10-04 for #1187-#1191) are linked from their rows in 3.2.

---

## 0. Re-entry procedure

Do this in order before touching anything.

1. **Orient.** Read this file, then
   [`architecture/08-defect-catalogue.md`](architecture/08-defect-catalogue.md). Read `08` before
   `01`-`07`; those predate the adversarial review and carry correction boxes.
2. **Sync first, then re-verify.** Every file:line in the catalogue and in `docs/review/` is
   potentially stale after an upstream move. Re-verify a finding against the current tree before
   fixing it; never fix from a remembered line number.

   ```bash
   git fetch upstream --prune
   git log --oneline HEAD..upstream/dev        # what is new
   ```

   Port upstream changes by cherry-pick. After icssw-org squash-merges a PR of ours, merge
   (not rebase) `upstream/dev` into `fork-dev`. See `/rebase-upstream`.

3. **Establish the baseline** so before/after has a "before":

   ```bash
   tools/regression.sh --stage 1,2                      # must be green
   pio run -e heltec_wifi_lora_32_V3 -e wiscore_rak4631 # note RAM/Flash figures
   ```

4. **Pick the next item** from section 3.2, work it, and satisfy section 2.5 before committing.

---

## 1. Goals

Stated across the 2026-08 session, grouped by theme. Nothing here is superseded unless marked.

### 1.1 Understand the system

| #   | Goal                                                                                                |
| --- | --------------------------------------------------------------------------------------------------- |
| G1  | Answer honestly: is this spaghetti code, or a core with modules? Where is the kernel?               |
| G2  | Create drillable code documentation covering structure, duplication, tangled and complex code       |
| G3  | **"X-ray vision" over the whole software** - a new contributor should be able to act, not just read |

### 1.2 Modernise safely

| #   | Goal                                                                                                             |
| --- | ---------------------------------------------------------------------------------------------------------------- |
| G4  | Bring dependencies up to date: what is current, what breaks, which hardware is at risk, what needs testing       |
| G5  | Decide whether a **1:1 rewrite** makes sense, with unit/integration/regression tests for before/after comparison |
| G6  | Reach **modern, tested** firmware                                                                                |

### 1.3 Make correctness structural

| #   | Goal                                                                                                                                                      |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G7  | Type-check every variable and buffer so that **buffer overflows become structurally impossible**, not merely absent                                       |
| G8  | Map which state is touched by which **CPU core / task / ISR**, so atomics are used **exactly where** there is genuine concurrent access, and nowhere else |

### 1.4 Ship it

| #   | Goal                                                                                        |
| --- | ------------------------------------------------------------------------------------------- |
| G9  | Track upstream, layer our commits on top                                                    |
| G10 | **One finding, one commit, one upstream PR**, so the maintainer can take them one at a time |
| G11 | Fix "everything" locally so we hold the finished, fixed firmware                            |
| G12 | Timing (all at once vs. spread over weeks): **decided 2026-09-03, all at once.**            |

### 1.5 Non-negotiable working rules

These constrain _how_ every other goal is met.

- **Zero tolerance for breakage.** This is production firmware on a live network.
- **Always before/after comparison.** No change ships without evidence of what it changed.
- **Adversarial review** is mandatory, not optional.
- **"Don't guess, test it, eyeball it. Don't assume, show it."**

---

## 2. Plan

### 2.1 The decisive insight

G11 (fix everything) and the zero-tolerance rule cannot both be met until an oracle exists.
"Before/after" by flashing hardware and watching does not scale to 30 board variants. So the
harness comes first, and every fix carries its own evidence. Delivered: see 3.1.

### 2.2 What the adversarial review changed

The first concept (docs 01-07) was reviewed by 8 independent finders; the corrections are in
[`architecture/08-defect-catalogue.md`](architecture/08-defect-catalogue.md) section 1. The ones
that changed the plan:

| ID   | What was wrong                                                          | Consequence                                    |
| ---- | ----------------------------------------------------------------------- | ---------------------------------------------- |
| C-03 | The "golden-vector corpus you already own" does not exist               | Replaced by the oracle in 2.3                  |
| C-02 | "Radio interface collapses ~3,200 duplicated lines": real overlap ~15 % | The #1 structural recommendation was withdrawn |
| C-01 | `OnRxDone` does not run in interrupt context on either platform         | Concurrency model re-derived (G8)              |
| C-04 | Four boards run Arduino 2.0.14, not 2.0.17                              | Real fleet split in the dependency sequence    |
| C-05 | The proposed `[nrf52]` cleanup would have broken two shipping boards    | Withdrawn as written                           |
| C-16 | "Large contributions are structurally unmergeable upstream"             | Refuted: small PRs merge, G10 is well-founded  |

Authoritative for nRF52 task priorities and the timer-service-task caveat:
[`architecture/09-concurrency-map.md`](architecture/09-concurrency-map.md). The 39 verified
findings of `code-audit-20260712.md` (`SEC-01` ... `TEST-39`) are adopted under their own IDs.

### 2.3 Answers to the open questions

**G5, rewrite 1:1? No.** Reasoning in
[`architecture/05-rewrite-vs-refactor.md`](architecture/05-rewrite-vs-refactor.md): the tests
needed to validate a rewrite must exist before it, so incremental work is strictly cheaper.

**The replacement oracle (after C-03 killed golden vectors):**

1. **Differential testing:** pre-fix and post-fix logic in one native binary, assert they agree.
2. **A real capture path:** `captureFrame()`/`captureDrain()` (`src/capture_functions.cpp`) dump
   accepted frames as raw bytes (RX under `--loradebug on`, TX under `--txcapture on`).
3. **Hand-authored specification vectors**, written from the spec, not from the decoder's output.
   Only these catch a pre-existing bug; the other two are regression fences.

### 2.4 Delivery shape

Every item is one commit with evidence in the message, ready to become one upstream PR. Commit
bodies are German because they become PR descriptions; the documentation set is English. Each fix
commit carries the mechanism, a concrete failing input, the verification performed and the
RAM/flash delta.

### 2.5 Definition of Done — per fix

| #   | Requirement                                                                                                                            |
| --- | -------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | The finding is **re-verified against the current tree**: file:line confirmed, not remembered                                           |
| 2   | The failure is **demonstrated**, not argued: a concrete input, a computed bound, or a native repro                                     |
| 3   | A regression test exists that **fails before and passes after**, or the demonstration from (2) is recorded verbatim in the commit body |
| 4   | The host gate is green (`tools/regression.sh --stage 1,2`, or the affected `pio test -e native*`)                                      |
| 5   | Every **affected** environment builds (both MCU families if the file is shared)                                                        |
| 6   | RAM/flash delta recorded against the pre-fix baseline                                                                                  |
| 7   | Commit body in German: mechanism, triggering input, verification, delta, `Refs:` to the finding ID                                     |
| 8   | **One finding per commit.** If you touched two, split them.                                                                            |

For anything non-trivial, add an adversarial pass that tries to refute the fix before it is
committed.

### 2.6 Commands

```bash
# Gate (host: native Unity envs + golden selftest + tool suites)
tools/regression.sh --stage 1,2
tools/regression.sh --stage all           # plus bench fleet over USB (stage 3, never run yet)
# Never run a bare `pio test`: it walks the board envs and flashes attached hardware.
# One `pio` process at a time (shared build cache).

# Single native env
pio test -e native

# Build a target / both MCU families
pio run -e heltec_wifi_lora_32_V3
pio run -e heltec_wifi_lora_32_V3 -e wiscore_rak4631
pio run                                   # all default_envs

# Which environments exist, and what flags does one really get
pio project config --json-output
pio run -e <env> -v | grep -- '-D BOARD_'

# Static analysis and structure metrics
pio check -e heltec_wifi_lora_32_V3
python3 tools/arch_metrics.py src
python3 tools/arch_duplication.py src 12

# RAM/flash snapshot (hardcodes 7 targets, see section 6)
python3 tools/ram_snapshot.py

# Upstream sync
git fetch upstream --prune && git log --oneline HEAD..upstream/dev
```

Project skills, in `.claude/commands/` (gitignored, local only; see `REG-06`):

| Skill               | Use                                                          |
| ------------------- | ------------------------------------------------------------ |
| `/full-regression`  | the end-to-end gate (`tools/regression.sh`) with one verdict |
| `/rebase-upstream`  | the documented upstream sync procedure                       |
| `/build-firmware`   | build the main targets and copy to Desktop                   |
| `/flash-rak`        | build, convert and flash the RAK4631 via UF2                 |
| `/release-firmware` | cut and publish a fork release incl. the web flasher         |
| `/ram-snapshot`     | RAM/flash comparison doc                                     |
| `/code-audit`       | audit against `docs/codequality-rules.md`                    |
| `/submit-pr`        | draft + submit a PR to upstream **dev** (German description) |
| `/logauswertung`    | analyse captured serial logs                                 |

### 2.7 Upstream PR mechanics

- Target is the `dev` branch of `icssw-org/MeshCom-Firmware`. The PR description is German and
  detailed: which code changed, and why.
- PR branches are built, not branched: `git checkout -b pr/<topic> upstream/dev`, then take the
  firmware files only from `fork-dev`, squash to one commit. No docs, tools, tests or debug code
  in a PR. After upstream squash-merges, merge `upstream/dev` into `fork-dev`.
- DK5EN has write access upstream but never self-merges; Kurt reviews and merges.
- **Open PRs (2026-10-16):** #1213 and #1214. Small surgical PRs are the proven path.
- Use `/submit-pr --dry-run` first to review the drafted description.

---

## 3. Steps

The former 3.1 (what the hardening campaigns delivered) is in [`archive/backlog-rows-20261107.md`](archive/backlog-rows-20261107.md).

### 3.2 Open work

Verified against git and the code on 2026-11-07 (135 rows: 37 closed and moved, 98 open; CLK-01, six W1 follow-ups and two W2 follow-ups added in the last two days). One line per item; the full history of every row is in
[`archive/backlog-rows-20261107.md`](archive/backlog-rows-20261107.md) (search by id). No item waits for an
operator decision.

#### Release train

The next release is built in one pass:

1. **Implement** every `impl` row. Wave W1 is done on `fork-dev` (committed 2026-11-07, coding only, no bench): BL-01, RM-NOANS-01 and TEST-OTA-01 closed; BLE-SET-01, SB-06, GPIO-06 and IDEE-04 moved to their bench, field and upstream tables. Wave W2 is done on `fork-dev` (2026-11-08, coding only): SENS-04 and CSMA-01 moved to the bench table. Waves W3 (CLK-01 instrumentation) and W4 (GOLDEN-MACROS-01 regenerated, closed, not gated by decision) are done 2026-11-08. Remaining (2): CLK-01 (W0 done; W1 before-measurement joins the bulk bench, then the fix wave), CSMA-03 (not in this train, operator 2026-11-07). The six W1 follow-up rows (BL-01-FU, BLE-SET-01-FU, GPIO-08, SB-07, CLK-TOOL-01, RM-TXT-02) and the two W2 follow-ups (CSMA-05, SENS-05) are optional.
2. **Bulk build:** `tools/regression.sh --stage 1,2`, then every board env once (sequential, one `pio`).
3. **Bulk flash:** DK5EN-1, -14, -90, -92 with that build (not DK5EN-98).
4. **Bulk bench:** every `bench` row (32) in one session: SENS-04, CSMA-01, BLE-SET-01, SB-06, TD-22, TD-23, NRF-BLEDFU, SNF-ACK-01, CSMA-02, CSMA-04, TXP-02, BLK-01, AU-14, BAT-05, BOOT-01, TD-27, TD-28, TD-29, TD-30, TD-32, TD-33, WEB-DARK-01, WEB-DARK-02, WEB-A11Y-01, WEB-LED-01, WEB-LED-02, WEB-DUP-01, TD-34, RM-Q-01, TD-THEME-01, WEB-PWD-01, WEB-GZ-01.
5. **Docs:** release notes, RESUME, campaign docs; move rows that closed to the archive.
6. **Commit, push `fork-dev`, release** with `/release-firmware` (CI builds and publishes).

Already on `fork-dev` and unreleased, so they ship with this train: GPIO-01..05, GPSV4-01 W1, TD-THEME-01 W1-W3.
After the release: `soak` rows (DK5EN-98 with OK), the `upstream` PR, field checks when the hardware is there.

#### Implement (firmware and tooling) (10)

Code still to write. Everything here goes into the next bulk build.

| Id            | Area                       | Item                                                                                                                                                                                                                                                                                                                                                                                                                                     | Status 2026-11-07                                                                                                                                         | Next step                                                                                                                                                                     |
| ------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CSMA-03       | LoRa TX / nRF52 (upstream) | nRF52 CAD is asynchronous, so channel-busy can be missed between CAD result and TX.                                                                                                                                                                                                                                                                                                                                                      | Not in this release train (operator 2026-11-07): nRF52 only, needs bench proof with CSMA-04 stamps.                                                       | Act on CAD result in callback or re-check IRQ before send                                                                                                                     |
| CLK-01        | Clock / displays           | Display clock lags 1-2 min then jumps (field report T3 S3 DK1OM-3): OLED clock frozen on message page, 15 s redraw gate, free-running clock loses loop latency per tick.                                                                                                                                                                                                                                                                 | W0 done 2026-11-08: helpers and tests (fix criteria ignored), INSTRUMENT [CLOCK]/[DISPCLK], --timesrc, --clockstat, --oledshot, --tdshot, clock_drift.py. | W1 before-measurement: INSTRUMENT flash DK5EN-1/-14/-92, clock_drift.py run, then fix wave W2                                                                                 |
| BL-01-FU      | Mesh                       | Stale `--nbrreport` mentions in comments (`loop_functions.cpp` ~6007-6014 and ~6085-6089, `esp32_main.cpp` ~5095, `nrf52_main.cpp` ~2343, `web_functions.cpp` ~2969, `tools/nbrlog.py`) and on the Pages page `docs/presentation/Nachbarschaftsmatrix.html`; `node_sset4` bits 0x0100/0x0200 to be marked retired (INSTRUMENT builds keep stale bits).                                                                                   | Found 2026-11-07 (W1 advisor)                                                                                                                             | Clean up comments and page, mark the bits retired                                                                                                                             |
| BLE-SET-01-FU | nRF52 / BLE                | Remove the dead `applyPendingBleSettings` path, `extern g_lora_data` in `WisBlock-API.h`, `settings_rx_callback` comments, and the source scrapes in `test_command_setters` / `test_command_toggles` that pin it.                                                                                                                                                                                                                        | Found 2026-11-07 (W1 advisor)                                                                                                                             | Remove dead path and scrapes                                                                                                                                                  |
| GPIO-08       | Pin policy / T-Deck Pro    | T-Deck Pro `configuration.h` carries T-Deck copies: `TDECK_SDCARD_CS` 39 (= GPS_EN on the Pro, real SD CS 48 per `src/t-deck-pro/utilities.h`) and `TDECK_TOUCH_INT` 16 (real 12); harmless today because the Pro build excludes `src/t-deck/`.                                                                                                                                                                                          | Found 2026-11-07 (W1 advisor)                                                                                                                             | Correct or remove the two defines                                                                                                                                             |
| SB-07         | Safeboot                   | On the `SAFEBOOT_APP_VALID_VIA_SET_BOOT_PARTITION` fallback path an identity rejection comes after `esp_ota_set_boot_partition` succeeded (boot pointer not undone); no env builds this path today.                                                                                                                                                                                                                                      | Found 2026-11-07 (W1 advisor)                                                                                                                             | Undo the boot pointer on rejection, or drop the path                                                                                                                          |
| CLK-TOOL-01   | Tests / bench tools        | `clock_drift.py` hardening: wait for the boot line before startup commands, restore `--timesrc` at the end, close ports, call `identity_guard`, tests for the `--yes-reboot` and busy-port refusals.                                                                                                                                                                                                                                     | Found 2026-11-07 (W1 advisor)                                                                                                                             | Harden the tool, add the refusal tests                                                                                                                                        |
| RM-TXT-02     | Remote / sender text       | RM WAITING text says "can take up to a minute" (`rm_sender_policy.h` ~:352), no longer fits the 171 s timeout.                                                                                                                                                                                                                                                                                                                           | Found 2026-11-07 (W1 advisor)                                                                                                                             | Reword the text, update the pinned test                                                                                                                                       |
| CSMA-05       | LoRa TX / CSMA             | CSMA-01 leftovers: armed priority and the shortened wait persist across idle re-anchors (`esp32_main` ~:4246, `nrf52_main` ~:1748) until the next compute, so a later low-priority frame can inherit a short wait; case-A/B relay backoff path records jitter 0, so a re-arm there has no spread; external-radio busy back-off overwrites armed prio without csma_timeout; no test drives `csmaRearmOnEnqueue` through `addTxRingEntry`. | Found 2026-11-08 (W2 advisor)                                                                                                                             | Reset armed prio and shortened wait on idle re-anchor, give the relay path a real jitter, set csma_timeout in the busy back-off, add the `addTxRingEntry` test to test_txring |
| SENS-05       | Sensors                    | SENS-04 leftovers: `sanitize_dht_type` is not called from the settings load path, so --info/--persiststat can show an out-of-range value (schema range 11..22 lets 15 through); loop-task-only invariant not noted at the DHT delete; web GUI 1-Wire info row does not show the type; help-line padding of `--dht`.                                                                                                                      | Found 2026-11-08 (W2 advisor)                                                                                                                             | Call `sanitize_dht_type` from `sanitize_loaded_settings`, add the invariant comment, show the type in the web 1-Wire row, fix the help padding                                |

#### Bench test and measure (32)

Code is in `fork-dev`; a test or measurement on DK5EN-1, -14, -90 or -92 is left. One bulk bench session after the bulk flash.

| Id          | Area                                  | Item                                                                                                                          | Status 2026-11-07                                                                                                                                                                                                                                                                                                                                                                                            | Next step                                                                                                                |
| ----------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| TD-22       | T-Deck / heap                         | T-Deck message tabs used too much heap; now capped at 16 tabs with a 40 KiB heap floor.                                       | Fixed and released in v4.40a.10.25, merged upstream with #1238; only operator GUI check of eviction/trim open.                                                                                                                                                                                                                                                                                               | Operator visual check of tab eviction on DK5EN-14                                                                        |
| TD-23       | T-Deck / display                      | T-Deck backlight level was not persisted and had no day/night dimming.                                                        | Implemented, released v4.40a.10.25, upstream #1238; operator UAT on bench open; dark theme now TD-THEME-01.                                                                                                                                                                                                                                                                                                  | Operator UAT of backlight day/night on DK5EN-14                                                                          |
| NRF-BLEDFU  | Flash / nRF52 (RAK4631, T114, T-Echo) | nRF52 firmware update over Bluetooth with the Nordic app, using the already-started DFU service.                              | Decided 2026-11-07: accept unauthenticated DFU as is, document the risk; bench run on DK5EN-90 pending.                                                                                                                                                                                                                                                                                                      | Bench BLE DFU update on DK5EN-90, document risk                                                                          |
| SNF-ACK-01  | Store-and-Forward                     | Store node delivered a DM twice when the recipient had already acked it.                                                      | Fixed e95e5dd9, released v4.40a.10.25; not benched over the air.                                                                                                                                                                                                                                                                                                                                             | Optional over-the-air double-delivery check on bench nodes                                                               |
| CSMA-02     | LoRa TX / CSMA (upstream)             | CSMA slot is sized 35 ms but the real blind window from CAD start to TX is 51-64 ms.                                          | Open; needs un-inflated measurement via CSMA-04 instrument builds before sizing.                                                                                                                                                                                                                                                                                                                             | Measure with CSMA-04, then resize slot or jitter                                                                         |
| CSMA-04     | LoRa TX / measurement                 | Blind windows were measured with LORADEBUG on, giving only an upper bound.                                                    | Instrumentation (cad/free/tx stamps) built, in v4.40a.11.06; measurement run on three nodes not done.                                                                                                                                                                                                                                                                                                        | Flash INSTRUMENT build to three nodes, repeat matched run                                                                |
| TXP-02      | Gateway / UDP                         | UDP PM to the gateway's own call was not in the dedup ring, so LoRa copy uploaded twice.                                      | Fixed 2026-11-02, released v4.40a.11.06; bench check open.                                                                                                                                                                                                                                                                                                                                                   | Bench UDP DM then LoRa copy, expect one GWU                                                                              |
| BLK-01      | RX / blocklist                        | Call blocklist `--blockcall` replacing the third-party DM3KS build blocklist.                                                 | Done, released v4.40a.10.23, upstream #1238; legacy-key migration not benched.                                                                                                                                                                                                                                                                                                                               | Bench legacy-key migration on a bench node                                                                               |
| AU-14       | Auto update / watchdog                | Classic ESP32 T-Beam rebooted by task watchdog during auto-update download.                                                   | Single event 2026-10-17, cause unknown, no later evidence.                                                                                                                                                                                                                                                                                                                                                   | Reproduce on DK5EN-92 with LoRa traffic, capture USB backtrace                                                           |
| BAT-05      | Battery / ADC boards                  | A cell hot-plugged into a running Heltec V3 was detected only after about 5 minutes.                                          | Fixed (3-sample streak), released v4.40a.11.06; hot-plug bench on DK5EN-1 open.                                                                                                                                                                                                                                                                                                                              | Hot-plug a cell into DK5EN-1, expect present within 2 min                                                                |
| BOOT-01     | Display / boot screen (all boards)    | Boot screen said 'MeshCom 4.0' while firmware is 4.40.                                                                        | Fixed with one MESHCOM_TITLE macro, released v4.40a.11.06; visual check on boards open.                                                                                                                                                                                                                                                                                                                      | Check boot screen on DK5EN-1 OLED and DK5EN-14                                                                           |
| TD-27       | T-Deck / position table               | T-Deck position table call column was too narrow and wrapped long calls.                                                      | Fixed 2026-11-02, released v4.40a.11.06; photo check on DK5EN-14 open.                                                                                                                                                                                                                                                                                                                                       | Photograph position, MHeard and Path tables on DK5EN-14                                                                  |
| TD-28       | T-Deck / message input                | T-Deck destination field next to the send button jumps vertically while typing and clips text.                                | Fixed in code, released in v4.40a.11.06; no bench photo confirming the fix yet (2026-11-07).                                                                                                                                                                                                                                                                                                                 | Photograph DM field while typing 1-9 chars on DK5EN-14.                                                                  |
| TD-29       | T-Deck / GPS page                     | T-Deck GPS page font is too large, text wraps and DIR is cut off under SEND POS.                                              | Fixed in code, released in v4.40a.11.06; bench photo still open; colon alignment cosmetic.                                                                                                                                                                                                                                                                                                                   | Photograph GPS page on DK5EN-14.                                                                                         |
| TD-30       | T-Deck / header icons                 | T-Deck header BLE and WiFi icons should be tappable toggles with persisted state.                                             | Implemented, released in v4.40a.11.06; DK5EN-14 bench of toggle, reboot persistence and AU with BLE off open.                                                                                                                                                                                                                                                                                                | Bench on DK5EN-14: tap icons, reboot, AU with BLE off.                                                                   |
| TD-32       | T-Deck Pro / BLE                      | T-Deck Pro lacks a BLE toggle in its UI; T-Deck WiFi icon skipped the webserver-off sequence.                                 | Fixed, released in v4.40a.11.06; hardware check open. T-Deck part benchable, Pro needs the device.                                                                                                                                                                                                                                                                                                           | Check T-Deck icon off/on on DK5EN-14; Pro later.                                                                         |
| TD-33       | T-Deck / header icons                 | BLE and WiFi header icon colours differ; both should show on/off the same way.                                                | Fixed (lime on, grey struck off), released in v4.40a.11.06; WiFi-off state not yet checked.                                                                                                                                                                                                                                                                                                                  | Check WiFi off by hand on DK5EN-14; Pro colours later.                                                                   |
| WEB-DARK-01 | Web GUI / theme                       | Web GUI has no dark theme following the OS colour scheme.                                                                     | Implemented and released in v4.40a.11.06; Chrome check on real node pages still open.                                                                                                                                                                                                                                                                                                                        | Chrome light/dark check of all pages on a bench node.                                                                    |
| WEB-DARK-02 | Web GUI / theme                       | Chart segment colours in dark mode fall below 3:1 contrast against the track.                                                 | Fixed with CSS variables, released in v4.40a.11.06; Chrome dark-mode check open.                                                                                                                                                                                                                                                                                                                             | Chrome dark check of Neighbours and queue chart.                                                                         |
| WEB-A11Y-01 | Web GUI / accessibility (suggestion)  | Selected left-menu icon turns black on dark background; wanted BITV contrast and at most four font sizes.                     | Fixed (currentColor icons, tokens, AA tests), released in v4.40a.11.06; Chrome check open.                                                                                                                                                                                                                                                                                                                   | Chrome contrast check of selected menu entry with WEB-DARK-01.                                                           |
| WEB-LED-01  | Web GUI / board LED                   | Setup offers board LED only as heartbeat on/off; wanted a persisted off / flash / steady choice.                              | Implemented, released in v4.40a.11.06; bench of LED modes, reboot persistence and RM1 override open.                                                                                                                                                                                                                                                                                                         | Test off/flash/steady from Setup and console on bench LED boards.                                                        |
| WEB-LED-02  | Web GUI / board LED                   | Board LED off/flash/steady buttons have uneven width on the Setup page.                                                       | Fixed (.mcledbtn one width), released in v4.40a.11.06; visual Chrome check open.                                                                                                                                                                                                                                                                                                                             | Chrome check of Setup LED buttons.                                                                                       |
| WEB-DUP-01  | Web GUI / Setup                       | Setup page uses id extudp twice (IP field and switch), so getElementById finds only the first.                                | Fixed with extudpip id, released in v4.40a.11.06; Chrome save check open; upstream still has the bug.                                                                                                                                                                                                                                                                                                        | Chrome check IP field and switch save; then upstream PR.                                                                 |
| TD-34       | T-Deck / bench                        | --disptest drops the console connection or reboots the node (INSTRUMENT only); console wifi on/off does not repaint the icon. | Open; ble-off icon fixed in 11.06, disptest cause unproven, repaint cosmetic open.                                                                                                                                                                                                                                                                                                                           | Run --disptest square 8 on DK5EN-14 with serial capture.                                                                 |
| RM-Q-01     | Web GUI / Remote page                 | RM Queues card Read lost answers over several hops because commands were sent on a fixed timer.                               | Fixed on fork-dev with per-row Read and answer-stepped sending, unreleased; JS tests only.                                                                                                                                                                                                                                                                                                                   | Check Queues card Read via RM1 between bench nodes.                                                                      |
| TD-THEME-01 | T-Deck / display                      | Light and dark colour theme for T-Deck and T-Deck Plus display matching the web GUI.                                          | W1-W3 committed, unreleased; W4 bench check on DK5EN-14 pending (2026-11-07).                                                                                                                                                                                                                                                                                                                                | W4 bench screenshots on DK5EN-14.                                                                                        |
| WEB-PWD-01  | Web GUI / web password                | Web GUI could not set, change or clear its own login password.                                                                | Implemented, released in v4.40a.11.06; Chrome set/change/clear check still open.                                                                                                                                                                                                                                                                                                                             | Bench: set, change, clear password in Chrome on a node.                                                                  |
| WEB-GZ-01   | Web GUI / gzip assets                 | Gzip static assets add several connections per cold page load on the single-threaded web server.                              | Released in v4.40a.11.06; works in browser, load case with cold cache untested.                                                                                                                                                                                                                                                                                                                              | Cold-cache load test on ESP32 and RAK4631, two browsers.                                                                 |
| BLE-SET-01  | nRF52 / BLE                           | nRF52 settings characteristic 0xF0A1 is dead on air because setFixedLen(2001) exceeds the 510 limit.                          | Code done 2026-11-07 (W1): 0xF0A1 removed, source-scrape test; unreleased, not benched.                                                                                                                                                                                                                                                                                                                      | nRF Connect on DK5EN-90 shows no 0xF0A1; settings via phone still work                                                   |
| SB-06       | Safeboot                              | Safeboot `app_valid` check should be stricter and verified with a deliberately wrong image.                                   | Code done 2026-11-07 (W1): hdr/chip/segs/desc checks after esp_image_verify; no project-name check (operator decision), no SHA-256; not benched.                                                                                                                                                                                                                                                             | Upload a wrong-chip and a non-app image to Safeboot on one bench board, expect why;chip / why;desc and recovery stays up |
| SENS-04     | Sensors                               | One-wire DHT path only accepts DHT11, DHT21/DHT22 are never read.                                                             | Code done 2026-11-08 (W2): `--dht` takes 11 or 22, persisted (key node_dht, default 11, sanitized), DHT built after settings load on the `--owgpio` pin; DHT21 not supported. Bench: connect a DHT22 to a bench node, `--dht 22`, `--onewire on`, expect temperature and humidity; check --info and reboot persistence. Release note: DHT now uses the configured `--owgpio` pin (was the compile-time pin). |
| CSMA-01     | LoRa TX / CSMA                        | A reply to a received frame gets no priority on the first CSMA attempt.                                                       | Code done 2026-11-08 (W2): a higher-priority enqueue shortens the running CSMA wait (shorten only, no extra random draw); not benched. Bench: repeat matched run C (RM1 reply first CAD expected 3000-3350 ms) with CSMA-04 stamps.                                                                                                                                                                          |

#### Soak and production gateway (4)

Long runs or anything on DK5EN-98 (explicit OK each time). Run after the release, in parallel to other work.

| Id        | Area                      | Item                                                                                                               | Status 2026-11-07                                                                                      | Next step                                                 |
| --------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------ | --------------------------------------------------------- |
| GWRX-01   | Gateway / TX              | Gateway transmitted a server-injected frame after hearing the same id on LoRa.                                     | Fixed, released v4.40a.10.23, benched ESP32 and nRF52; count-mode night on DK5EN-98 needs operator OK. | Count-mode night on DK5EN-98 with operator OK             |
| REBOOT-98 | Stability / gateway       | Production gateway DK5EN-98 rebooted once without panic text on 2026-10-04.                                        | Single event, cause unknown; watch only.                                                               | If it repeats: read reset reason, check router/power      |
| AU-24     | Auto update / T-Beam heap | On DK5EN-92 after hours of uptime Download refuses with lowheap_tls, and a first Download hit TASK_WDT in gateway. | Open, cause unknown; seen at 4.8 h uptime on 44075ef3, likely also on 11.06.                           | Reproduce on DK5EN-92 after long uptime with console log. |
| WEB-TX-01 | Web GUI / T-Beam          | Web send guard cut a page on the T-Beam, leaving incomplete HTML and a 2 s loop stall.                             | Stall windows and chunked responses released in v4.40a.11.06; 60 of 60 loads fine, cause unconfirmed.  | Watch for a repeat on the T-Beam.                         |

#### Field (16)

Needs conditions or hardware the bench does not have (sensors, T-Deck Pro, OLED, Windows, field captures).

| Id          | Area                                            | Item                                                                                                         | Status 2026-11-07                                                                                                                              | Next step                                                                 |
| ----------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| TD-25       | T-Deck / T-Deck Pro / message length            | T-Deck/T-Deck Pro message input capped characters, not bytes, so multi-byte text could be cut mid-character. | Fixed, released v4.40a.11.06, upstream #1249; DK5EN-14 benched; T-Deck Pro (e-paper refresh) not benched, no unit in fleet.                    | Check input limit on a T-Deck Pro                                         |
| TD-26       | T-Deck Pro / DM                                 | On T-Deck Pro a 9-character destination call was not treated as a DM and went out as broadcast.              | Fixed 2026-11-02, released v4.40a.11.06; hardware check on T-Deck Pro open.                                                                    | Test 9-char DM and invalid destination on a T-Deck Pro                    |
| SENS-03     | Sensors                                         | RM `sens` did not report DHT and DS18B20 sensors.                                                            | Fixed 2026-11-02 (sweep W1), released v4.40a.11.06; needs DHT11 and DS18B20 hardware on one-wire pin, not in fleet.                            | Bench with DHT11 and DS18B20 when sensors available                       |
| LED-TRACKER | LED                                             | Heltec Wireless Tracker has no LED macro; sources disagree on GPIO18 vs RGB on 48.                           | Still open; needs tracker hardware.                                                                                                            | Verify LED pin on a Wireless Tracker                                      |
| LED-V4R8    | LED / Heltec V4                                 | Heltec V4 R8 env would drive PSRAM GPIO35 as LED; LED is on GPIO46.                                          | Still open; needs R8 hardware and own env.                                                                                                     | Create V4R8 env, verify on R8 board                                       |
| LED-FIELD   | LED                                             | LED pins and polarity of T-Echo, T114, T-ETH-Elite and S3 DevKitC are unconfirmed.                           | Still open; no bench board.                                                                                                                    | Field confirmation from owners of these boards                            |
| ETH-04      | nRF52 (RAK4631)                                 | RAK4631 got no DHCP lease after reboot or IP loss at DB0AIS.                                                 | Fixed 2026-10-11 in v4.40a.10.11, bench-verified on DK5EN-90; field confirmation at DB0AIS and upstream PR outstanding.                        | Field confirmation at DB0AIS with an image                                |
| AU-21       | Auto update / TLS heap (ESP32-S3 without PSRAM) | Heltec Wireless Tracker TLS failed from low internal heap (TFT sprite) with a misleading error.              | Fixed e6e74bc6/c100451a, released v4.40a.10.24; not verified on tracker hardware.                                                              | Verify on a Wireless Tracker if one is available                          |
| WF-01..04   | Web flasher / autodetect                        | Web flasher autodetect issues reported for T-Beam 1W and T-Deck Pro.                                         | Fixed, shipped v4.40a.10.21, web flasher live; Windows check outstanding.                                                                      | Windows run of the web flasher                                            |
| WF-05       | Web flasher / autodetect                        | Board identity unknown without a running app in the web flasher.                                             | Fixed and shipped v4.40a.10.21; CI marker check exists (verify_release_assets.py); Windows check outstanding.                                  | Windows run of the web flasher                                            |
| BAT-04      | Battery / PMU                                   | Battery charge state and current were not read or shown on PMU boards.                                       | Implemented, benched on AXP2101 boards, upstream #1250, released v4.40a.11.06; AXP192 and state `full` unverified.                             | Verify on an AXP192 board; comment on #1073                               |
| WX-01       | Sensors / telemetry                             | Optional LTR390 UV sensor and reed-switch anemometer telemetry.                                              | Implemented, benched without sensors, upstream #1251, released v4.40a.11.06; field test needed.                                                | Field test with real LTR390 and anemometer, comment on #999               |
| BAT-04b     | Battery / BQ boards                             | Charge state and current for BQ25896/BQ27220 on T-Deck Pro and T5.                                           | Implemented host-only, released v4.40a.11.06; no hardware bench.                                                                               | Bench T-Deck Pro and T5 with and without a cell                           |
| GPIO-07     | Field test / pins                               | GPIO-01 pin features are untested on hardware; wanted OM field reports after release.                        | Decided to release GPIO-01 unbenched; waits for OM reports after the release.                                                                  | Collect OM field reports on T-Beam, Heltec V3 after release.              |
| HV4-VEXT-01 | Power / Heltec V4                               | Heltec V4 Vext pin was not held during deep sleep.                                                           | Partly answered by GPSV4-01 deep-sleep hold, unreleased; no current measurement.                                                               | Measure sleep current on a Heltec V4.                                     |
| GPIO-06     | Pin policy / T-Deck Pro                         | T-Deck Pro defines TDECK_KEYBOARD_INT as 46, same as LoRa EN; correct is GPIO 15; enable pins unreserved.    | Code done 2026-11-07 (W1): T-Deck Pro TDECK_KEYBOARD_INT 15, pins 15/38/39/41 reserved in kHard[] with static_asserts; not tested on hardware. | Keyboard works, pins page shows 15/38/39/41 reserved (needs a T-Deck Pro) |

#### Waiting on others (11)

Reporter, maintainers, McApp, OE1KBC/OE1KFR talk. Nothing to do until they answer.

| Id         | Area                      | Item                                                                                   | Status 2026-11-07                                                                                                                                                                                                                                                             | Next step                                                        |
| ---------- | ------------------------- | -------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| RM-NVS-01  | Remote management / ESP32 | RM 'remember password' answers 'store' on an E22 node, hypothesis: NVS partition full. | Diagnostics released in v4.40a.10.20; waiting for reporter's --info and save log.                                                                                                                                                                                             | Wait for reporter's --info and [RMNODES] log                     |
| SNF-GW     | Store-and-Forward         | Store node as gateway taking PMs from the central server.                              | Firmware upstream (#1238), off by default; server side waits for Kurt's decision.                                                                                                                                                                                             | Wait for Kurt (OE1KBC) on server handing PMs to store nodes      |
| DEF-HN     | Upstream data             | HN upload by gateways so mcmap gets named leaf neighbourhoods.                         | Deferred; trigger is mcmap MC-319 shipping and proving the graph too thin.                                                                                                                                                                                                    | Wait for mcmap MC-319                                            |
| BLC-01..05 | BLE                       | BLE reconnect/advertising robustness fixes and counters.                               | Done on fork-dev 2026-10-04, upstream #1191/#1238; only reply to app maintainers on #1191 remains.                                                                                                                                                                            | Post #1191 reply with section 8.4 for app maintainers            |
| TRK-02     | Web flasher / button      | A Heltec V3 had TRACK set after a web flasher update; no flasher-driven cause found.   | Not reproducible; TRK-03 boot fix released; reopen only with new log.                                                                                                                                                                                                         | Wait for reporter log showing TripleClick or prior TRACK state   |
| IDEE-06    | Policy                    | Define permissible use on the QRG and what counts as spam.                             | Kurt and OE1KFR own the usage policy; fork offers technical levers.                                                                                                                                                                                                           | Wait for Kurt and OE1KFR policy.                                 |
| IDEE-07    | Lifecycle                 | End of life for firmware below 4.40a: server refuses, firmware blocks.                 | Support agreed; propose a date later than 31.12.2026 to Kurt, he announces.                                                                                                                                                                                                   | Propose later date to Kurt.                                      |
| EMCOMM-SYN | EMCOMM                    | Assessment of DJ8MEH's 13 EMCOMM proposals; most operators will not sign.              | Position drafted: priority only for signed frames; not agreed with OE1KBC/OE1KFR; open: bounded priority for unsigned. Proof design 2026-11-07: no signature fits the RM1 budget per frame, signed batch instead ([concept 4.9.1-4.9.7](mesh-evolution-concept-20261021.md)). | Answer DJ8MEH; send regulatory questions to DARC, BNetzA, OeVSV. |
| IDEE-15    | Server                    | High availability with two master servers and hub servers.                             | Kurt decides; fork supports.                                                                                                                                                                                                                                                  | Wait for Kurt.                                                   |
| IDEE-16    | Server                    | KEEP in parallel to two hub servers.                                                   | Firmware side waits for next server generation, then needs a trigger for parallel KEEP.                                                                                                                                                                                       | Wait for next server generation.                                 |
| IDEE-18    | Growth                    | US and Japan expansion and capacity for many new OMs.                                  | Kurt decides; fork supports with US/JP band presets.                                                                                                                                                                                                                          | Wait for Kurt.                                                   |

#### Upstream PR (2)

Fork fix done; the next step is a PR against upstream `dev`.

| Id       | Area            | Item                                                                                      | Status 2026-11-07                                                                                | Next step                                                             |
| -------- | --------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------- |
| GPSV4-01 | GPS / Heltec V4 | Heltec V4 GNSS never worked: swapped UART, unpowered kit, sensor I2C on GNSS lines.       | W1 fix on fork-dev, unreleased, untested on hardware; upstream PR and groups.io reply pending.   | Cut upstream PR via /submit-pr marked hardware-untested; draft reply. |
| IDEE-04  | Channel / hops  | Relay does not cap the hop nibble, so old-firmware frames with nibble 8-15 keep circling. | Code done 2026-11-07 (W1): relayHopNext() clamps nibble 8-15 to 7 before decrement, native test. | Upstream PR for the hop clamp                                         |

#### Studies and documents (5)

Analysis or writing, no firmware.

| Id           | Area                   | Item                                                                                      | Status 2026-11-07                                                                          | Next step                                             |
| ------------ | ---------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ | ----------------------------------------------------- |
| LOGREP-01    | Log analysis / tooling | Review the 2026-11-01 multi-node log-analysis session and its tooling.                    | Decided 2026-11-07 to do in a separate session; tools/logreport exists.                    | Review session of the log-report pipeline             |
| BUILD-PAR-01 | Build / tooling        | Parallel local board builds with cloned PlatformIO dirs were 1.8x slower than sequential. | Decided: root-cause the slowdown first; low priority, sequential is good enough.           | Run RCA builds with CPU timing per setup.             |
| IDEE-01      | Channel / measurement  | Measure before changing relay behaviour: histograms and PER per SNR bin.                  | Decided to pursue, starting with log data already in the repo.                             | Analyse repo logs for histograms and PER per SNR bin. |
| IDEE-19      | Hardware               | Hardware roadmap by guiding principles.                                                   | Decided: finish draft hardware-guiding-principles.md as basis for talk with OE1KBC/OE1KFR. | Finish the draft document.                            |
| IDEE-20      | Positioning            | Feature parity with Meshtastic and MeshCore, where and why we differ.                     | Decided: finish draft market-analysis-meshtastic-meshcore-20261104.md for the talk.        | Finish the draft document.                            |

#### Ideas (18)

Not committed. Pick up only on an operator decision.

| Id         | Area               | Item                                                                                     | Status 2026-11-07                                                                                                                                                               | Next step                                       |
| ---------- | ------------------ | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| MC5-TOPO   | Topology           | MeshCom 5 topology concept (Fassung 2).                                                  | Concept only; decided 2026-11-07 to collect field feedback, no code.                                                                                                            | Collect feedback, discuss in roadmap talk       |
| OPT-W4..W7 | DRY unification    | Remaining DRY unification waves of the one-shot PR plan (shared helpers not yet merged). | Decided 2026-11-07 to keep open; none is a defect.                                                                                                                              | Pick helpers one PR at a time if wanted         |
| HASH-TAG   | Groups             | Hashtag groups #TAG instead of fixed group numbers.                                      | User story only; kept open as idea for next talk with Kurt.                                                                                                                     | Raise with Kurt whether it goes upstream        |
| NRF-WEB    | Flash              | T-Echo and T114 link the whole web layer though only reachable with Ethernet hardware.   | Open, low priority; not needed for flash space.                                                                                                                                 | Only act if T-Echo/T114 run short of flash      |
| CMD-UNIFY  | Security           | Make RM1 grammar the canonical command layer over the console, one registry.             | Parked 2026-11-07; concept done, no code; default answers Q1-Q5 recorded.                                                                                                       | Pick up with waves W1-W5 only when wanted       |
| RTC-06     | Time / nRF52       | On nRF52 bMyClock stays false with RTC and GPS fix, so node_date_* are not refreshed.    | Open, pre-existing, no evidence of a fix.                                                                                                                                       | Re-check when nRF52 time path is next touched   |
| STRUCT     | Structure          | Structural epics left alone under the minimal-change rule (DRY, SIMP, ALT, STATE).       | Decided 2026-11-07 to keep open as deferred.                                                                                                                                    | Fold into OPT-W4..W7 or close as won't-do       |
| DEF-ARD    | Dependencies       | Arduino 3.x migration and Arduino 2.0.17 on four lagging boards.                         | Deferred until REG-07 per 2026-11-07 decision.                                                                                                                                  | Re-evaluate once REG-07 is done                 |
| NBR-FW-01  | Topology / web GUI | Firmware letter is stored twice in the topology data (direct slot and mheard view).      | Parked, no defect, dead data.                                                                                                                                                   | Remove when the direct slot layout changes next |
| IDEE-02    | Channel / capacity | Multi-SF, regional SF or backbone layer, for more capacity.                              | Decided: later, after IDEE-01 data; concept only.                                                                                                                               | Wait for IDEE-01 data.                          |
| IDEE-03    | Channel / PN       | PN priority: ACK first, own place in the TX pool, retries.                               | Decided to pursue with per-source rate limit; concept only.                                                                                                                     | Draft concept with CSMA-01 and TX pool.         |
| IDEE-05    | Channel / limits   | Rate limits per node and message type, enforced in node and server.                      | Decided: later, values only after measuring.                                                                                                                                    | Wait for IDEE-01 measurements.                  |
| IDEE-08    | Lifecycle          | Expiry date in future builds with a latch to renew or lock out.                          | Decided: later; key holder and clock open.                                                                                                                                      | None until key holder is clarified.             |
| IDEE-11    | Apps               | Real status messages for BLE and web clients instead of text matching.                   | Decided: later; outlook in backpressure-protocol.md section 5.                                                                                                                  | Wait.                                           |
| IDEE-12    | EMCOMM             | Priority flag plus signed messages with trust agreed between OMs.                        | Decided: later; priority only for signed frames; regulatory check open. Signature design: signed batch ([concept 4.9.1-4.9.7](mesh-evolution-concept-20261021.md), 2026-11-07). | Regulatory check, then concept.                 |
| IDEE-13    | PN routing         | Path discovery and more than 4 hops for personal messages.                               | Decided: later, after IDEE-17; concept only.                                                                                                                                    | Wait for IDEE-17.                               |
| IDEE-14    | APRS               | Real APRS WX frame and multi-line position, converted by gateway or server.              | Decided: later; concept only.                                                                                                                                                   | Wait.                                           |
| IDEE-17    | Topology           | Roles in HEY and elected relays in cities.                                               | Decided: later; concept only.                                                                                                                                                   | Wait.                                           |

## 4. State of the repository

### 4.1 Branch model

- **`fork-dev`** is the only working branch: `upstream/dev` plus the fork's docs, tests, tools,
  GitHub Pages and fork-only firmware (`src/`, `platformio.ini`, `variants/`) not upstream yet. New
  firmware lands on `fork-dev` first; PRs are cut from `upstream/dev` later.
  `origin/fork-dev` is the GitHub default branch.
- `gh-pages` holds the web flasher and the published concept pages (only the latest release is
  offered, `--keep 1`).
- **Retired 2026-10-02:** `fork-main`, `fork-neo`, `fork-neo-test`, `master` (tag
  `archive/fork-neo-test-20261002`). Nothing points at them any more.
- **Upstream sync:** cherry-pick, no merge of fork history. After icssw-org squash-merges a PR of
  ours, merge (not rebase) `upstream/dev` to re-sync.
- **PRs** are built from `upstream/dev` plus firmware files only, target upstream `dev`, text in
  German, one squashed commit. Debug code goes into the dedicated instrumentation files
  (`src/instrument.*`, `src/test_inject.*`, `src/t-deck/tdeck_debug.*`) with one-line hooks.
- **Upstream state (2026-10-24, synced `ff67ecd5`):** PR #1186 (4.40a) merged as `e1e2acea`; #1200, #1201, #1207, #1208,
  #1213, #1214, #1224, #1233, #1235, #1236 merged since. No PR open, no PR worktree left.
- Historical branch tables (to 2026-09-18) and the old branch model are in the archive journal.

### 4.2 Bench fleet

From `tools/bench/fleet.json`, state 2026-10-24. DK5EN-1, DK5EN-90 and DK5EN-92 run normal `fork-dev`
`a7b21de5` images (gateway on, `--mesh off`, 2 dBm); DK5EN-14 was not reachable on 2026-10-24.

| Node     | Board           | USB serial              | Host (2026-10-24)           |
| -------- | --------------- | ----------------------- | --------------------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E        | 192.168.68.75 (ETH)         |
| DK5EN-14 | T-Deck Plus     | E0:72:A1:AD:65:E0 (MAC) | `dk5en-14.local` (not seen) |
| DK5EN-92 | T-Beam v1.2     | 573C000584              | 192.168.68.71               |
| DK5EN-1  | Heltec V3       | 0001                    | 192.168.68.73               |

Hosts come from DHCP with short leases and go stale within hours: confirm the callsign on the start
page or with `identity_guard.py --port` before any flash or test.
Resolve ports by USB serial (ioreg or pyserial), never by name; ports move. Opening a port reboots
every ESP32; the RAK needs DTR; the T-Beam flashes at 460800. DK5EN-98 is the production Heltec V3
(logger on rpizero). Bench rules: test traffic only to group `TEST` or a direct message to an own
node, never broadcast, never a foreign callsign, TX power at most 2 dBm,
`tools/bench/identity_guard.py` before any test, no port open while `serial_capture.py` holds it.
The RAK web GUI has no mDNS (Ethernet path ships no responder); reach it by IP.

---

## 5. Where to read what

Index of `docs/`, rebuilt 2026-09-11 and extended 2026-10-03. Everything not listed here is
closed and lives in [`archive/`](archive/README.md) with a status box saying why. `RESUME.md` is
the hand-over; this file carries the item rows. Rows reading `archive/...` point at documents
that were archived after the live rows they explain closed or went bench-only.

### 5.1 The distilled architecture set

| Question                                              | Document                                            |
| ----------------------------------------------------- | --------------------------------------------------- |
| Is it spaghetti? Where is the kernel?                 | `architecture/01-system-overview.md`                |
| How do 30 variants map onto one tree?                 | `architecture/02-build-and-variants.md`             |
| What is outdated and what breaks?                     | `architecture/03-dependencies.md`                   |
| Where is the code unmaintainable?                     | `architecture/04-complexity-and-duplication.md`     |
| Should we rewrite?                                    | `architecture/05-rewrite-vs-refactor.md`            |
| Which test layers, and why                            | `architecture/06-test-strategy.md`                  |
| What can be observed/driven; bench design             | `architecture/07-verification-infrastructure.md`    |
| **What is broken, in what order, with what evidence** | `architecture/08-defect-catalogue.md`               |
| Which core/task touches which state (goal G8)         | `architecture/09-concurrency-map.md`                |
| Every buffer, its size and its bounds (goal G7)       | `architecture/10-buffer-inventory.md`               |
| Wire format: LoRa / server UDP / EXTUDP / BLE         | `architecture/11-wire-format.md` (+ `.html` render) |
| Raw evidence behind 08/09/10                          | `review/2026-07-31/`                                |

> **Read `08` before acting on `01`-`07`.** Those were written before the adversarial review and
> carry correction boxes pointing at `08 §1`.

`review/2026-07-31/` holds the nine unedited reports from the review that produced `08`, `09` and
`10`. They are a snapshot of 2026-07-31 written before the rebase: **their line numbers are stale
by construction.** Treat the distilled documents as current and these as provenance.

### 5.2 Protocol and format references — read before changing a frame

| Question                                                        | Document                                              |
| --------------------------------------------------------------- | ----------------------------------------------------- |
| What the MCP23017 `/D=` field is and why `D`                    | `mcp23017-digital-field.md`                           |
| Every `/X=` position key, who parses it, who loses it           | `archive/aprs-parser-drift-20260911.md`               |
| EXTUDP telemetry: protocol reference / practical guide          | `ext_udp_telemetry.md` / `ext_udp_telemetry_guide.md` |
| Back-pressure states, thresholds and notice wording             | `backpressure-protocol.md`                            |
| External radio over TCP (issue #1015, draft)                    | `external-radio-protocol.md`                          |
| Endianness, CONF frame, settings persistence                    | `wire-compat.md`                                      |
| Every setting: register, command, flash offset                  | `settings-registers.md`                               |
| NTP and time handling                                           | `ntp-timing.md`                                       |
| Who acknowledged? (ACK attribution, normative decisions)        | `ack-wer-hat-quittiert.md`                            |
| ACK attribution: firmware implementation plan, stages 1-4       | `archive/ack-implementierungsplan.md`                 |
| Priority queue, Trickle-HEY, flood networking (German)          | `prio-talk-flood-networking.md`                       |
| LoRa TRX design notes (German, historical but still referenced) | `../README_LORA_TRX.md`                               |
| Serial output under `--loradebug`                               | `loradebug-serial-output.md`                          |

### 5.3 Open defects and analyses — the reading behind live backlog rows

| Row(s)                        | Document                                                                                                                                                                          |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GPS-05b`, `GPS-10`           | `bug-baro-altitude-20260906.md`                                                                                                                                                   |
| `GPS-01`..`04` (closed, kept) | `archive/bug-GPS-uart-overflow-20260901.md`                                                                                                                                       |
| `NET-01`..`06`                | `bug-static-ip-dns-20260901.md`                                                                                                                                                   |
| `ETH-04`                      | `issue-rak-dhcp-after-reboot-db0ais.md`                                                                                                                                           |
| `N-25` (closed, kept)         | `archive/bug-N25-gps-baud-scan-watchdog.md`                                                                                                                                       |
| `MEM-04`                      | `archive/mem-headroom-classic-esp32-20260905.md`                                                                                                                                  |
| `SL-01`..`07` (bench owed)    | `setlog-instrumentation-impl-plan-20260902.md`                                                                                                                                    |
| `TD-15`                       | `archive/tdeck-map-empty-after-reboot-rca-20260905.md`                                                                                                                            |
| `TD-09`..`TD-11`              | `tdeck-findings-20260828.md`, `tdeck-gui-verdict.md`, `archive/review-tdeck-gui-20260828.md`, `archive/tdeck-baseline-20260828.md`, `tdeck-lvgl-agent-guide.md`, `lvgl-research/` |
| deepsleep follow-ups          | `gpio-hold-and-hwcdc-followups.md`                                                                                                                                                |
| `DM-01`..`DM-06`              | `archive/proposal-dm-transport-reliability-20260909.md`, `archive/konzept-dm-transportsicherung-20260909.html`                                                                    |
| `OPT-01`..`04`, `OPT-D1..D13` | `optimization-audit-20260910.md` (+ `-appendix.md`), `testplan-dry-unification-20260910.md`, `archive/dry-unification-gantt-20260910.html`                                        |
| `CQ-02`..`CQ-12`              | `code-quality-2.0.md`, `codequality-rules.md`                                                                                                                                     |
| `WEB-03` (c)-(e)              | `archive/finding-webgui-passwort.md`                                                                                                                                              |
| `AU-16`                       | `troubleshooting-tbeam-1w-autoupdate.md`                                                                                                                                          |
| upstream reports we filed     | `issue-ble-i-register-mtu-20260828.md`, `issue-mh-json-size-budget-20260828.md`                                                                                                   |

### 5.4 Bench runbooks — how to run the proof

| Test                                         | Document                               |
| -------------------------------------------- | -------------------------------------- |
| Full regression over USB serial, four nodes  | `automation-runner-runbook.md`         |
| Inventory of every test suite and the runner | `test-suite-map.md`                    |
| AP-reboot recovery (TM-38)                   | `bench-ap-reboot.md`                   |
| EXTUDP both directions (TM-43)               | `bench-extudp-regression.md`           |
| OTA re-flash (TM-40)                         | `bench-ota-regression.md`              |
| T-Deck colour/geometry display test (TM-41)  | `tdeck-display-test.md`                |
| GPS and I2C sensor bench                     | `archive/gps-sensor-bench-20260822.md` |

### 5.5 Reports and measurements kept as provenance

| Subject                                                   | Document                                                                                                   |
| --------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| The 39 pre-existing findings                              | `code-audit-20260712.md`                                                                                   |
| Earlier audits and their fixes                            | `archive/code-audit-20260508.md`, `archive/code-audit-20260626.md`, `archive/code-audit-fixes-20260627.md` |
| WiFi: driver analysis, fix report, night soak             | `wifi-findings-20260829.md`, `archive/wifi-report-20260830.md`, `archive/wifi-soak-report-20260831.md`     |
| HEY storms and Trickle-HEY suppression                    | `archive/hey-storm-analysis-20260827.md`, `hey-supp.md`                                                    |
| BLE: inventory and TX latency                             | `archive/ble-review.md`, `archive/report-ble-tx-latency.md`, `archive/NimBLE.md`                           |
| Ping on the T-Beam 1W (track mode, not a board defect)    | `archive/ping-fix.md`                                                                                      |
| OE3 mountain nodes / adaptive relay slot (German, HTML)   | `archive/report-2026-09-02-oe3-bergknoten.htm`, `archive/report-2026-09-02-adaptiver-relay-slot.htm`       |
| All 355 upstream issues, harvested                        | `archive/upstream-issue-harvest-20260829.md`                                                               |
| Review of upstream PR #1114 (KISS/TCP, merged + reverted) | `archive/pr1114-kiss-review-20260901.md`                                                                   |
| T-Deck map code analysis (German, HTML)                   | `archive/tdeck-karte-codeanalyse.html`                                                                     |

### 5.6 Proposals and designs not yet decided

| Subject                                                                                   | Document                                                                                          |
| ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| DM transport reliability - the line being pursued                                         | `archive/proposal-dm-transport-reliability-20260909.md`                                           |
| DM store-and-forward outbox - the alternative, not pursued                                | `archive/concept-dm-store-and-forward.md`                                                         |
| Hashtag groups `#TAG` instead of fixed group numbers (FW 4.36)                            | `userstory-hashtag-filter.md`                                                                     |
| ADR 02: network-importance relay backoff (draft, rev. 3)                                  | `adr-nc-importance-backoff.md`                                                                    |
| ADR: TOTP remote LED control (proposed)                                                   | `archive/adr-totp-remote-led.md`                                                                  |
| Store node as gateway (concept, 2026-10-02)                                               | `snf-gateway-concept-20261002.md`                                                                 |
| Open upstream issues #1187-#1191: concept and wave plans (2026-10-04)                     | `concept-open-issues-20261004.md`, recon in `concept-open-issues-20261004-recon/`                 |
| MeshCom 5 topology concept                                                                | `meshcom5-topologie/`                                                                             |
| National characters on E290 / Wireless Paper and T-Deck (CHARS-01, upstream #1221)        | `concept-national-chars-20261101.md`                                                              |
| UV and wind telemetry, ESP32 only (WX-01, upstream #999)                                  | `campaign-wx01-uv-wind-20261102.md`                                                               |
| Ideas until end of 2027 for discussion, hardware principles, market analysis (2026-11-04) | `ROADMAP.md`, `hardware-guiding-principles.md`, `market-analysis-meshtastic-meshcore-20261104.md` |
| Talks and slides                                                                          | `presentation/`                                                                                   |

### 5.7 Journals

| Journal                                                              | Document                                       |
| -------------------------------------------------------------------- | ---------------------------------------------- |
| Backlog campaign journal 2026-08-18 to 2026-09-29 (this file's past) | `archive/backlog-journal-20260818-20260929.md` |
| Backlog rows closed, archived 2026-10-24                             | `archive/backlog-closed-20261024.md`           |
| Hand-over journal 2026-09-03 to 2026-10-01                           | `archive/resume-journal-20260903-20261001.md`  |
| Hand-over journal 2026-10-05 to 2026-10-23                           | `archive/resume-journal-20261005-20261023.md`  |
| Release history                                                      | `release-journal.md`                           |

## 6. Known gaps in this documentation set

Recorded so they are not mistaken for completeness.

| Gap                                                                                                                                                  | Impact                                                              |
| ---------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| **No persistence/flash-migration document.** `FLASH_VERSION` does not migrate, and there are two incompatible `meshcom_settings` layouts (`N-12`).   | A field change to the settings struct is currently unsafe on nRF52. |
| **No boot/OTA document** beyond the safeboot contract. One `ota_0` slot means no rollback by construction; five boards have no remote update at all. | Unknown recovery path after a bad update.                           |
| `tools/ram_snapshot.py` hardcodes 7 targets, so "RAM baseline across all envs" is not executable as written (`08` C-12).                             | Baseline is partial.                                                |
