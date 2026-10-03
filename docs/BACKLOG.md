# Backlog - MeshCom Firmware Hardening Campaign

Working document for picking the campaign back up: what we set out to do, how we decided to get
there, what is still open. The dated campaign journal (2026-08-18 to 2026-09-29) lives in
[`archive/backlog-journal-20260818-20260929.md`](archive/backlog-journal-20260818-20260929.md).

## Stand 2026-10-03

- **Branch model:** `fork-dev` is the only working branch: `upstream/dev` (icssw-org) plus one port
  commit carrying fork-only docs, tests, tools and GitHub Pages. HEAD `cfcfcb3c`. `fork-main`,
  `fork-neo`, `fork-neo-test` and `master` are retired (tag `archive/fork-neo-test-20261002`).
- **Upstream:** PR #1186 (4.40a: Store-and-Forward, Nachbarschaftsmatrix, serial config) merged
  2026-10-02 as `e1e2acea`. No open upstream PR by dk5en. DK5EN never merges upstream himself.
- **Last release:** `v4.40a.10.02` from fork-dev `08b7dd8b`, firmware byte-identical to official
  v4.40a. Releases are manual (`/release-firmware`); both GitHub Actions workflows are disabled.
- **Gate:** `tools/regression.sh --stage 1,2` (host) or `--stage all` (plus bench). Today: 48
  native envs, 1627 Unity cases, 467 pytest, golden selftest. Stage 3 (bench fleet) has never run
  against hardware. Inventory: [`test-suite-map.md`](test-suite-map.md).
- **Open work:** section 3.2, one table. 8 of its rows are the open points of the regression
  runner (`REG-01`..`REG-08`).
- **Journal:** the 7000-line campaign log (sections 3.8a to 3.8bb, 3.9, the Durchgang entries,
  old branch tables) moved to the archive file above, headings unchanged. Citations of the form
  "BACKLOG.md 3.8x" resolve there. Hand-over: [`RESUME.md`](RESUME.md). Release detail:
  [`release-journal.md`](release-journal.md).

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
- **No PR is currently open.** Small surgical PRs are the proven path.
- Use `/submit-pr --dry-run` first to review the drafted description.

---

## 3. Steps

### 3.1 Done

What the campaigns delivered (commit-level evidence and the per-item rows are in the archive
journal and in [`release-journal.md`](release-journal.md)):

- **Analysis (G1-G8):** architecture set `01`-`11`, defect catalogue `08`, concurrency map `09`,
  buffer inventory `10`, wire-format specification `11`; adversarial review as the standing method.
- **Security and correctness fixes** from the 39-finding audit and the catalogue (`SEC-02..06`,
  `N-03..N-06`, `BUG-07..13`, `CONC-14..19`, `N-13` over-sync); `N-01`, `N-02`, `N-07`, `N-11`
  accepted as risk by maintainer decision 2026-08-18. All fixes upstream since PR #1102.
- **Test oracle:** native Unity suite grew from 11 to 1627 cases over 48 envs; differential
  runner, 13-frame corpus, spec vectors, golden capture and drift-matrix lint (`test/golden/`).
- **Bench instrumentation:** `--injectraw`, `--loratx`, `--spitrace`, loop breadcrumbs, per-board
  harnesses (T-Deck, OLED, RAK), OTA regression and abort benches, identity guard, `meshlogger`.
- **DRY unification, phases A-D:** audit of 2026-09-10 turned into twins, golden baseline,
  decisions and waves `W1`-`W3` (settings struct merge incl. frozen BLE layout). The remainder is
  in 3.2 (`OPT-W4..W7`, `OPT-E`).
- **Safeboot OTA:** status page, Auto-AP, state machine, abort bench on three boards.
- **Store-and-Forward and PN retry (XOR format)**, **Nachbarschaftsmatrix** (stages 1 and 2),
  serial config: all in upstream 4.40a (PR #1186).
- **4.40a upstream merge:** 2026-10-02, branch model reduced to `fork-dev`; last release
  `v4.40a.10.02`.
- **Regression runner:** `tools/regression.sh` (stages 1-3), `docs/test-suite-map.md`, skill
  `/full-regression`, commit `cfcfcb3c`.
- **Field fixes since 4.35s:** DM transport, T-Deck keylock and CDC back-pressure, W5100S socket
  state, nRF52 stack overflow in the loop task (`N-22`), battery path (`BAT-01..03`), `N-36`.

### 3.2 Open work

Ids marked `BL-nn` were assigned on 2026-10-03; all other ids are the original ones. Status is
the state on 2026-10-03. Anything not listed here is closed and lives in the archive journal.

| Id              | Area              | Item                                                                                                                                                                                                                                                                          | Status                                                                                                                                                                                                                                                                                                                                   | Owner/next step                                                                                                                         | Reading                                                                                          |
| --------------- | ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| N-36            | WiFi / ESP32      | Fresh ESP32 with no stored SSID never started its softAP                                                                                                                                                                                                                      | Fixed on fork-dev `42dbf03a`; German PR text drafted in `pr-draft-n36-20261003.md` (sweep W7)                                                                                                                                                                                                                                            | Operator: review the draft, submit with /submit-pr (src only)                                                                           | `git show 42dbf03a`                                                                              |
| SOAK-440A       | Release           | 4.40a soak on DK5EN-98 (OTA 2026-10-01 23:27, rpizero logger until 10-02 08:00)                                                                                                                                                                                               | Evaluated 2026-10-03: `soak-20261001-440a-verdict.md`, stable 8.5 h, 0 reboots; downlink silent in the capture, live again on 10-03 (see BL-07)                                                                                                                                                                                          | Closed; BL-07 carries the follow-up                                                                                                     | [`soak-20260929-verdict.md`](soak-20260929-verdict.md)                                           |
| DR-16 / REG-03  | Drift matrix      | Verdict `nrf52-changes` contradicts the code comment in `src/nrf52/nrf52_main.cpp` near `bTeleFirst` (bAllStarted port deliberately not done); the one row keeping `--phase implementation`                                                                                   | Decision pending                                                                                                                                                                                                                                                                                                                         | Choose: (a) re-decide as `both-valid` / documented non-change, or (b) port and test; then drop `--phase` from `test/golden/selftest.sh` | [`testplan/drift-matrix.csv`](testplan/drift-matrix.csv)                                         |
| BL-01           | Mesh              | `--mesh off` still transmits on two RF paths (gateway DM-ACK, DM-store custody)                                                                                                                                                                                               | Operator decision pending, not a defect                                                                                                                                                                                                                                                                                                  | Decide: gate both paths on `--mesh off`, or document them as intended                                                                   | [`commands-store-node.md`](commands-store-node.md)                                               |
| BL-02           | RX / UDP          | RX buffer never cleared between receives; `MAX_APRS_FRAME_SIZE` 340 > `UDP_TX_BUF_SIZE` 255                                                                                                                                                                                   | Re-verified 2026-10-03: real but bounded. OnRxDone clamps to UDP_TX_BUF_SIZE (`src/lora_functions.cpp:730`), buffer not zeroed between receives (`:739`); decodeAPRS never sees >255 B                                                                                                                                                   | Low: zero the tail after the memcpy when the file is next touched; no native target today                                               | [`architecture/10-buffer-inventory.md`](architecture/10-buffer-inventory.md)                     |
| BL-03           | GPS               | Altitude Kalman re-seed: `ALT_KF_RESEED_N` 10 too short; offline replay says 60 or a 30 m gate                                                                                                                                                                                | Closed 2026-10-03: `ALT_KF_RESEED_N` is already 60 (`src/gps_filter.h:24`), `test_sechzig_ausreisser_seeden_neu` and `test_feldserie93_keine_reseeds` cover it                                                                                                                                                                           | None                                                                                                                                    | [`bug-baro-altitude-20260906.md`](bug-baro-altitude-20260906.md)                                 |
| GPS-05b         | GPS               | Baro fusion tau: shipped 30 min, bench sd 6.64 m vs 4 m gate; tau 2 h would meet it; nRF52 half (`GPS-08`) unverified                                                                                                                                                         | Open, operator decision: tau 2 h meets the 4 m gate (bench sd 3.55 m vs 6.61 m) but lets ~5 m of weather drift through (`bug-baro-altitude-20260906.md` 11.2); nRF52 GPS-08 half unverified                                                                                                                                              | Decide tau; if changed, update the three tests named in test_alt_fusion (sweep scout 2026-10-03)                                        | [`bug-baro-altitude-20260906.md`](bug-baro-altitude-20260906.md)                                 |
| BL-04           | Stability         | One unexplained TASK_WDT on DK5EN-1 on 2026-09-28 17:32; web send path the only candidate; no ELF or coredump                                                                                                                                                                 | Watch                                                                                                                                                                                                                                                                                                                                    | Keep a coredump-capable build on DK5EN-1; reopen on a second occurrence                                                                 | [`soak-20260928-verdict.md`](soak-20260928-verdict.md)                                           |
| BL-05           | Security          | `{SET}` from any mesh node can trigger a settings save per value flip (advisor finding, low, same as upstream)                                                                                                                                                                | Accepted-risk candidate                                                                                                                                                                                                                                                                                                                  | Decide with `N-02` (accepted 2026-08-18): document, or rate-limit saves                                                                 | [`architecture/08-defect-catalogue.md`](architecture/08-defect-catalogue.md)                     |
| SNF-GW          | Store-and-Forward | Store node as gateway, taking PMs from the central server                                                                                                                                                                                                                     | Concept only (`b44b2d57`), no code                                                                                                                                                                                                                                                                                                       | Review the concept with Kurt before any implementation                                                                                  | [`snf-gateway-concept-20261002.md`](snf-gateway-concept-20261002.md)                             |
| MC5-TOPO        | Topology          | MeshCom 5 topology concept, Fassung 2                                                                                                                                                                                                                                         | Concept only                                                                                                                                                                                                                                                                                                                             | Collect field feedback; no code before a decision                                                                                       | [`meshcom5-topologie/`](meshcom5-topologie/)                                                     |
| OPT-W4..W7      | DRY unification   | Waves `W4`-`W7` of the one-shot PR plan (`D2-10` first; `W6` owes `D1-01`, `D4-01/02`; `W5` decisions answered 2026-09-16)                                                                                                                                                    | Re-audited 2026-10-03 against 4.40a: structural rows present (D1-02, D1-04, D1-05, D3-05); helpers not merged (D1-07 at_cmd.h, D1-08 batt_detect, D3-01 aprsExtractTag, D3-02 finalizeAndSendAPRS, D4-01/02 ui_common, D2-06/07 command table); R1-04 re-verify (journal says done, scout saw boot-time alloc at loop_functions.cpp:429) | Pick helpers one PR at a time if still wanted; none is a defect                                                                         | [`optimization-audit-20260910.md`](optimization-audit-20260910.md)                               |
| OPT-E           | DRY unification   | Phase E: prove and ship (`E1`-`E5`, G2 after-run, upstream review)                                                                                                                                                                                                            | Superseded by the 4.40a merge; upstream review happened as PR #1186                                                                                                                                                                                                                                                                      | Nothing unless the re-audit of `OPT-W4..W7` finds a remainder                                                                           | [`testplan-dry-unification-20260910.md`](testplan-dry-unification-20260910.md)                   |
| OPT-D14 / RF-09 | DRY unification   | Three TUs without a native target (`loop_functions.cpp`, `sendExtern()`, `nrf52_main.cpp`); Poland `track_freq` unit mismatch on nRF52 (434.855 Hz)                                                                                                                           | Re-verified 2026-10-03: OPT-D14 addressed (countryProfile native); RF-09 is a REAL open defect: PL row `434.855f` lands in the Hz column on nRF52 (`src/country_profile.cpp:104`, comment 45-62), Polish nRF52 track beacon gets RADIOLIB_ERR_INVALID_FREQUENCY                                                                          | Sweep wave 2: per-platform track_freq for PL + twin test asserting 434855000 on nRF52                                                   | [`optimization-audit-20260910.md`](optimization-audit-20260910.md)                               |
| HASH-TAG        | Groups            | Hashtag groups `#TAG` instead of fixed group numbers (FW 4.36)                                                                                                                                                                                                                | User story, not decided                                                                                                                                                                                                                                                                                                                  | Decide with Kurt whether it goes upstream                                                                                               | [`userstory-hashtag-filter.md`](userstory-hashtag-filter.md)                                     |
| ADR-IMP         | Relay             | ADR 02: network-importance relay backoff (draft rev. 3)                                                                                                                                                                                                                       | Draft                                                                                                                                                                                                                                                                                                                                    | Decide after the Nachbarschaftsmatrix field data                                                                                        | [`adr-nc-importance-backoff.md`](adr-nc-importance-backoff.md)                                   |
| ADR-TOTP        | Security          | TOTP remote LED control                                                                                                                                                                                                                                                       | Proposed                                                                                                                                                                                                                                                                                                                                 | Decide accept or drop                                                                                                                   | [`adr-totp-remote-led.md`](adr-totp-remote-led.md)                                               |
| SB-05           | Safeboot          | Second app slot (today a single `ota_0`, no rollback by construction)                                                                                                                                                                                                         | Decision open                                                                                                                                                                                                                                                                                                                            | Decide; needs a partition-table change on every board                                                                                   | [`safeboot-ota-contract.md`](safeboot-ota-contract.md)                                           |
| SB-06           | Safeboot          | Stricter `app_valid` check, verified with a different image                                                                                                                                                                                                                   | Open                                                                                                                                                                                                                                                                                                                                     | Bench with a deliberately wrong image on one board                                                                                      | [`safeboot-ota-contract.md`](safeboot-ota-contract.md)                                           |
| SB-07           | Safeboot          | Upstream PR for `src/safeboot/` only                                                                                                                                                                                                                                          | Not filed; awaits operator review                                                                                                                                                                                                                                                                                                        | Operator review, then German PR text                                                                                                    | [`safeboot-ota-contract.md`](safeboot-ota-contract.md)                                           |
| REG-01          | Regression        | Run stage 3 on hardware for the first time (`bench_suite.py`, harness calls never run against a device)                                                                                                                                                                       | Open                                                                                                                                                                                                                                                                                                                                     | One node first (T-Beam DK5EN-92), `/full-regression 3` without `--flash`, then RAK, then `--flash`, then `--extudp`                     | `~/Desktop/regression-offene-punkte.md` (outside the repo)                                       |
| REG-02          | Regression        | Second copy of the topo_shadow raw logs (master only on `rpizero:~/meshlog/dk5en-98/`, 2026-09-21..24, 54 MB; test reads `~/Downloads/dk5en-98-nbr/`)                                                                                                                         | Done 2026-10-03 (sweep W1): runner looks in ~/Downloads, then ~/meshlog, then rpizero, mirrors fetches; second copy exists                                                                                                                                                                                                               | None                                                                                                                                    | same working paper                                                                               |
| REG-04          | Regression        | `tools/webgui_badge_test.js` into stage 3                                                                                                                                                                                                                                     | Done 2026-10-03 (sweep W2): `webgui badge <node>` step after OTA for esp32 nodes with host, `--no-badge`                                                                                                                                                                                                                                 | First hardware run (REG-01)                                                                                                             | same working paper                                                                               |
| REG-05 / TM-26  | Regression        | Cross-node mesh exchange as a tool (`tools/bench/mesh_exchange.py`: every node `--sendpos`, every other node must show it in `--mheard`)                                                                                                                                      | Done on the host 2026-10-03 (sweep W3): `tools/bench/mesh_exchange.py`, 22 pytest, guard before every send, `--loradebug off` in finally; plan step `mesh exchange` with >= 2 nodes                                                                                                                                                      | First live run is part of REG-01                                                                                                        | [`automation-runner-runbook.md`](automation-runner-runbook.md)                                   |
| REG-06          | Regression        | Version the `/full-regression` skill (`.claude/commands/` is gitignored)                                                                                                                                                                                                      | Done 2026-10-03: `.gitignore` allowlists `.claude/commands/full-regression.md`, the file is tracked (also closes CQ-11)                                                                                                                                                                                                                  | None                                                                                                                                    | same working paper                                                                               |
| REG-07          | CI                | CI runs only `native` and `native_aprs`; workflows disabled                                                                                                                                                                                                                   | Done 2026-10-03 (sweep W6): job `unit-tests` runs `tools/regression.sh --stage 1,2` and uploads the run dir; workflow still disabled                                                                                                                                                                                                     | Enable the workflow when CI is wanted again                                                                                             | `.github/workflows/ci-build.yml`                                                                 |
| REG-08          | Regression        | Runner summary guesses case counts from the log (selftest shows "15 checks", actually 115 self-tests plus 17 lints)                                                                                                                                                           | Done 2026-10-03 (sweep W1): selftest prints `selftest: N commands`, nbrlog scripts print `nbrlog: <name>: M checks`, runner reads them; mock unittest line no longer hides its exit status                                                                                                                                               | None                                                                                                                                    | same working paper                                                                               |
| W0.2            | Build flags       | Five ESP32 envs without `extends = esp32` lack `-Wall -Wextra -Werror` (`t_deck_pro`, `t5_epaper`, `vision-master-e213`, `vision-master-e290`, `wireless-paper`); nRF52 `heltec_t114`/`t_echo` not hardened                                                                   | Open (low)                                                                                                                                                                                                                                                                                                                               | Build each with warnings on, count, then add the flags                                                                                  | [`architecture/08-defect-catalogue.md`](architecture/08-defect-catalogue.md)                     |
| W0.6            | Tests             | Native test for CSMA timing math (`via_functions` and `compress_functions` have tests)                                                                                                                                                                                        | Half done 2026-10-03 (sweep W4): `test_compress` 12 cases in env native (three format bugs pinned as BUG); CSMA math (`csma_compute_timeout_prio`, `src/lora_functions.cpp:3623-3641`) has no native target                                                                                                                              | Carve the CSMA formula into a header with injectable random, test it (sweep wave 3)                                                     | [`architecture/06-test-strategy.md`](architecture/06-test-strategy.md)                           |
| STRUCT          | Structure         | Epics left alone under the minimal-change rule: `DRY-20/23/24`, `SIMP-26/27`, `ALT-31/32`, `STATE-28`                                                                                                                                                                         | Deferred                                                                                                                                                                                                                                                                                                                                 | Fold into `OPT-W4..W7` or close as won't-do                                                                                             | [`architecture/04-complexity-and-duplication.md`](architecture/04-complexity-and-duplication.md) |
| N-12            | Settings          | `FLASH_VERSION` does not migrate; two incompatible `meshcom_settings` layouts                                                                                                                                                                                                 | Closed 2026-10-03: one struct, `node_fversion` vs `FLASH_STRUCT_VERSION` on both platforms resets to defaults on mismatch (`esp32_main.cpp:910-925`, `nrf52_main.cpp:552-565`), config import gated by layout                                                                                                                            | None; a true migration stays a design choice, not a defect                                                                              | [`wire-compat.md`](wire-compat.md)                                                               |
| DEF-ARD         | Dependencies      | Arduino 3.x migration; Arduino 2.0.14 to 2.0.17 on the four lagging boards                                                                                                                                                                                                    | Deferred; trigger (baseline plus CI gate) partly met                                                                                                                                                                                                                                                                                     | Re-evaluate once `REG-07` is done                                                                                                       | [`architecture/03-dependencies.md`](architecture/03-dependencies.md)                             |
| DEF-HN          | Upstream data     | HN upload by gateways so mcmap gets named leaf neighbourhoods                                                                                                                                                                                                                 | Deferred; trigger: mcmap MC-319 built and its gateway graph proves too thin                                                                                                                                                                                                                                                              | Revisit when MC-319 ships                                                                                                               | [`meshcom5-topologie/`](meshcom5-topologie/)                                                     |
| BL-06           | Backlog           | Legacy open ids from the 2026-09-11 list, status unverified since: `MH-01`, `MH-03`, `MEM-04`, `GPS-10`, `INS-03`, `TD-09`, `TD-11`, `TD-15`, `E22-01`, `TLM-01..03`, `WF-01`, `TM-28`, `WEB-03` (c)-(e), `CQ-02..CQ-12`, `DM-01..DM-06`, `APRS-02..04`                       | Triaged 2026-10-03 (sweep scout): closed APRS-02/03/04 (PR #1142); obsolete DM-01..06 (replaced by S&F in 4.40a); parked TLM-01..03 (blocked by TLM-03); open: MH-01/03, MEM-04, GPS-10, INS-03, TD-09/11/15, E22-01, WF-01, TM-28, WEB-03 (c)-(e), CQ-02..CQ-10, CQ-12; CQ-11 closed by REG-06                                          | Hardware/decision items stay here; CQ-09 (ruff gate) and CQ-04 (dead lv_conf.h) go to the sweep waves 2-3                               | [`archive/backlog-journal-20260818-20260929.md`](archive/backlog-journal-20260818-20260929.md)   |
| BL-07           | Gateway           | Heartbeat-staleness watchdog in `gatewayService_esp32()` arms only after the first BEAT sets `last_upd_timer`; a node that receives no BEAT after boot never warns (seen on DK5EN-98 after the 4.40a OTA: 1023 keepalives, 0 BEAT for >= 8.5 h, cleared later without reboot) | Open, found 2026-10-03                                                                                                                                                                                                                                                                                                                   | Arm the watchdog from the first KEEP sent, not the first BEAT received; native test in test_gateway_service_twin                        | `soak-20261001-440a-verdict.md`                                                                  |

---

## 4. State of the repository

### 4.1 Branch model

- **`fork-dev`** is the only working branch: `upstream/dev` plus one port commit (fork-only docs,
  tests, tools, GitHub Pages). HEAD `cfcfcb3c`. `origin/fork-dev` is the GitHub default branch.
- `gh-pages` holds the web flasher and the published concept pages (only the latest release is
  offered, `--keep 1`).
- **Retired 2026-10-02:** `fork-main`, `fork-neo`, `fork-neo-test`, `master` (tag
  `archive/fork-neo-test-20261002`). Nothing points at them any more.
- **Upstream sync:** cherry-pick, no merge of fork history. After icssw-org squash-merges a PR of
  ours, merge (not rebase) `upstream/dev` to re-sync.
- **PRs** are built from `upstream/dev` plus firmware files only, target upstream `dev`, text in
  German, one squashed commit. Debug code goes into the dedicated instrumentation files
  (`src/instrument.*`, `src/test_inject.*`, `src/t-deck/tdeck_debug.*`) with one-line hooks.
- **Upstream state:** PR #1186 (4.40a) merged as `e1e2acea`, no open PR by dk5en.
- Historical branch tables (to 2026-09-18) and the old branch model are in the archive journal.

### 4.2 Bench fleet

From `tools/bench/fleet.json`, state 2026-10-01. Today nothing is attached.

| Node     | Board           | USB serial       | Port last seen                    | IP / link     |
| -------- | --------------- | ---------------- | --------------------------------- | ------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E | /dev/cu.usbmodem1101              | ETH DHCP      |
| DK5EN-14 | T-Deck Plus     | (native USB)     | /dev/cu.usbmodem2101              | WiFi          |
| DK5EN-92 | T-Beam v1.2     | 573C000584       | /dev/cu.usbserial-573C0005841     | 192.168.68.73 |
| DK5EN-1  | Heltec V3       | 0001             | /dev/cu.usbserial-0001 (detached) | 192.168.68.62 |

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
| ACK attribution: firmware implementation plan, stages 1-4       | `ack-implementierungsplan.md`                         |
| Priority queue, Trickle-HEY, flood networking (German)          | `prio-talk-flood-networking.md`                       |
| LoRa TRX design notes (German, historical but still referenced) | `../README_LORA_TRX.md`                               |
| Serial output under `--loradebug`                               | `loradebug-serial-output.md`                          |

### 5.3 Open defects and analyses — the reading behind live backlog rows

| Row(s)                        | Document                                                                                                                                                                          |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GPS-05b`, `GPS-10`           | `bug-baro-altitude-20260906.md`                                                                                                                                                   |
| `GPS-01`..`04` (closed, kept) | `bug-GPS-uart-overflow-20260901.md`                                                                                                                                               |
| `NET-01`..`06`                | `bug-static-ip-dns-20260901.md`                                                                                                                                                   |
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

| Subject                                                        | Document                                                |
| -------------------------------------------------------------- | ------------------------------------------------------- |
| DM transport reliability - the line being pursued              | `archive/proposal-dm-transport-reliability-20260909.md` |
| DM store-and-forward outbox - the alternative, not pursued     | `archive/concept-dm-store-and-forward.md`               |
| Hashtag groups `#TAG` instead of fixed group numbers (FW 4.36) | `userstory-hashtag-filter.md`                           |
| ADR 02: network-importance relay backoff (draft, rev. 3)       | `adr-nc-importance-backoff.md`                          |
| ADR: TOTP remote LED control (proposed)                        | `adr-totp-remote-led.md`                                |
| Store node as gateway (concept, 2026-10-02)                    | `snf-gateway-concept-20261002.md`                       |
| MeshCom 5 topology concept                                     | `meshcom5-topologie/`                                   |
| Talks and slides                                               | `presentation/`                                         |

### 5.7 Journals

| Journal                                                              | Document                                       |
| -------------------------------------------------------------------- | ---------------------------------------------- |
| Backlog campaign journal 2026-08-18 to 2026-09-29 (this file's past) | `archive/backlog-journal-20260818-20260929.md` |
| Hand-over journal 2026-09-03 to 2026-10-01                           | `archive/resume-journal-20260903-20261001.md`  |
| Release history                                                      | `release-journal.md`                           |

## 6. Known gaps in this documentation set

Recorded so they are not mistaken for completeness.

| Gap                                                                                                                                                  | Impact                                                              |
| ---------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| **No persistence/flash-migration document.** `FLASH_VERSION` does not migrate, and there are two incompatible `meshcom_settings` layouts (`N-12`).   | A field change to the settings struct is currently unsafe on nRF52. |
| **No boot/OTA document** beyond the safeboot contract. One `ota_0` slot means no rollback by construction; five boards have no remote update at all. | Unknown recovery path after a bad update.                           |
| `tools/ram_snapshot.py` hardcodes 7 targets, so "RAM baseline across all envs" is not executable as written (`08` C-12).                             | Baseline is partial.                                                |
