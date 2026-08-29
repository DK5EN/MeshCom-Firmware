# Backlog — MeshCom Firmware Hardening Campaign

Working document for picking the campaign back up. Records **what we set out to do**,
**how we decided to get there**, and **exactly where we stand**. Open work lives in
§3.2; §3.1 is the done-list kept for evidence.

_(Previously `resume.md` in the repository root.)_

Last updated 2026-08-29 (§3.8f: TM-20 out of the PR, TM-34 WiFi research added). Branch `v4.35p_prio`, rebased onto `upstream/dev`; T-Deck work on `tdeck-partial-refresh-trace`.

> ### Standing risk — read first
>
> **Status 2026-08-18.** Of the eight RF- or network-reachable defects, **four are now fixed on
> this branch** — `N-03` (CONF zero-fill), `N-04` (BLE length underflow), `N-05` (mheard heap
> over-read), `N-06` (web `t_io` index) — plus `SEC-02` (format string). None are upstream yet,
> so **the shipped fleet still runs all of them**.
>
> **Achtung bei `SEC-02`:** der Fix allein ist nicht vollstaendig. `d36fb66f` hat als
> Nebenwirkung jeden BLE-Verbindungsaufbau verhindert (`N-18`, auf Hardware per Bisect
> gefunden, behoben mit `bd10b636`). Wer `SEC-02` upstream einreicht, muss **beide** Commits
> zusammen nehmen — sonst liefert man eine tote BLE-Verbindung aus.
>
> **Three remain deliberately unfixed**, accepted by maintainer decision on 2026-08-18:
> `N-01` (`{MCP}` password bypass), `N-02` (`{SET}` unauthenticated routing change) and `N-07`
> (BLE command channel unauthenticated). `N-01`/`N-02` are accepted as risk. `N-07` because the
> effective fix is BLE bonding, which disconnects every existing phone app until the user
> re-pairs — an upstream decision, not a branch decision. Rationale in
> [`docs/architecture/08-defect-catalogue.md` §2](architecture/08-defect-catalogue.md).
>
> Getting the four fixes upstream is now the highest-value open item in this campaign.

---

## 0. Re-entry procedure

Do this **in order** before touching anything. Skipping step 2 is the trap that already
cost one wrong conclusion in this campaign.

1. **Orient.** Read this file, then
   [`docs/architecture/08-defect-catalogue.md`](architecture/08-defect-catalogue.md).
   Read `08` before `01`–`07`; those predate the adversarial review and carry correction
   boxes.

2. **Rebase first, then re-verify.** Upstream moved 35 commits in roughly three weeks.

   ```bash
   git fetch upstream --prune
   git log --oneline HEAD..upstream/dev        # what is new
   git rebase upstream/dev                     # or use the /rebase-upstream skill
   ```

   **After any rebase every file:line in the catalogue and in `docs/review/` is
   potentially stale.** Re-verify a finding against the current tree _before_ fixing it.
   Never fix from a remembered line number.

3. **Establish the baseline** before changing anything, so before/after has a "before":

   ```bash
   pio test -e native                                  # must be green
   pio run -e heltec_wifi_lora_32_V3 -e wiscore_rak4631 # note RAM/Flash figures
   ```

4. **Pick the next item** from §3.2, work it, and satisfy §2.5 (Definition of Done) before
   committing.

---

## 1. Goals

Stated across the session, grouped by theme. Nothing here is superseded unless marked.

### 1.1 Understand the system

| #   | Goal                                                                                                |
| --- | --------------------------------------------------------------------------------------------------- |
| G1  | Answer honestly: is this spaghetti code, or a core with modules? Where is the kernel?               |
| G2  | Create drillable code documentation covering structure, duplication, tangled and complex code       |
| G3  | **"X-ray vision" over the whole software** — a new contributor should be able to act, not just read |

### 1.2 Modernise safely

| #   | Goal                                                                                                              |
| --- | ----------------------------------------------------------------------------------------------------------------- |
| G4  | Bring dependencies up to date: what is current, what breaks, which hardware is at risk, what needs testing        |
| G5  | Decide whether a **1:1 rewrite** makes sense — with unit/integration/regression tests for before/after comparison |
| G6  | Reach **modern, tested** firmware                                                                                 |

### 1.3 Make correctness structural

| #   | Goal                                                                                                                                                       |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G7  | Type-check every variable and buffer so that **buffer overflows become structurally impossible**, not merely absent                                        |
| G8  | Map which state is touched by which **CPU core / task / ISR**, so atomics are used **exactly where** there is genuine concurrent access — and nowhere else |

### 1.4 Ship it

| #   | Goal                                                                                          |
| --- | --------------------------------------------------------------------------------------------- |
| G9  | Rebase onto upstream, layer our commits on top                                                |
| G10 | **One finding → one commit → one upstream PR**, so the maintainer can take them one at a time |
| G11 | Fix "everything" locally so we hold the finished, fixed firmware                              |
| G12 | Timing (all at once vs. spread over weeks) — **still open, to be decided**                    |

### 1.5 Non-negotiable working rules

These constrain _how_ every other goal is met.

- **Zero tolerance for breakage.** This is production firmware on a live network.
- **Always before/after comparison.** No change ships without evidence of what it changed.
- **Adversarial review** is mandatory, not optional.
- **"Don't guess, test it, eyeball it. Don't assume, show it."**

---

## 2. Plan

### 2.1 The decisive insight

The goals contain a **sequencing constraint that was not obvious at the start**:

> G11 (fix everything) and the zero-tolerance rule cannot both be met until an oracle
> exists. Right now "before/after" means flashing hardware and watching — which does not
> scale to 30 board variants and ~50 findings.

So the harness comes **first**, and every fix afterwards carries its own evidence.

### 2.2 What the adversarial review changed

The first version of the concept (docs 01–07) was reviewed by 8 independent finders. It
contained errors that would have sent work in the wrong direction. The corrections are in
[`docs/architecture/08-defect-catalogue.md` §1](architecture/08-defect-catalogue.md);
the ones that changed the plan:

| ID   | What was wrong                                                                                                                                                | Consequence for the plan                                                               |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| C-03 | The "golden-vector corpus you already own" **does not exist** (0 usable pairs from 17 logs)                                                                   | The whole before/after oracle had no foundation. Replaced — see §2.3                   |
| C-02 | "Radio interface collapses ~3,200 duplicated lines" — measured overlap is ~221–268 lines (≈15 %), none of it radio code                                       | The #1 structural recommendation was **withdrawn**                                     |
| C-01 | `OnRxDone` does not run in interrupt context on either platform. **The nRF52 half of this was itself corrected on 2026-07-31** — see the note below the table | The concurrency model had to be re-derived from scratch (G8)                           |
| C-04 | Four boards run Arduino 2.0.14, not 2.0.17                                                                                                                    | Dependency sequence gained a real, previously denied fleet split                       |
| C-05 | The proposed `[nrf52]` cleanup would have **broken two shipping boards**                                                                                      | Withdrawn as written                                                                   |
| C-16 | "Large contributions are structurally unmergeable upstream" was an assumption                                                                                 | Refuted: **24 PRs from this author are merged upstream, 0 open** — G10 is well-founded |

> **A correction inside a correction — read this before touching nRF52 concurrency.**
> `C-01` originally said the SX126x `"LORA"` task runs at priority 2 and preempts `loop()`.
> That is **wrong**: `board.cpp:44` defines `TASK_PRIO_NORMAL` as the macro `1` (because
> `#ifndef` cannot see the Adafruit core's `= 2` **enum**), so the task is created at
> priority 1 — the same as `loop_task` — and `configUSE_TIME_SLICING` is 0, so equal-priority
> tasks do not even round-robin. The genuine priority-2 preemptor is the **FreeRTOS timer
> service task**, which reaches `OnRxDone` via `RadioOnRxTimeoutIrq → RadioBgIrqProcess`
> and runs it on a **1 KB** stack. The conclusion (nRF52 has real preemption of the main
> loop; ESP32 does not) survives — the mechanism does not.
> Authoritative: [`docs/architecture/09-concurrency-map.md`](architecture/09-concurrency-map.md).

**Prior art the first concept missed entirely:** `docs/code-audit-20260712.md` (2026-07-12)
holds **39 already-verified findings** (`SEC-01` … `TEST-39`). Essentially all are still
open. The campaign adopts those IDs rather than re-deriving them.

### 2.3 Answers to the open questions

**G5 — should we rewrite 1:1?** **No.** Reasoning in
[`docs/architecture/05-rewrite-vs-refactor.md`](architecture/05-rewrite-vs-refactor.md).
Short version: the tests needed to validate a rewrite must exist _before_ the rewrite, so
the first work item is identical either way — and once the harness exists, incremental
work is strictly cheaper and shippable. The corrected reasoning (per C-02) is that the
blocker between the two MCU stacks is a differing **concurrency model**, not a differing
API, which a rewrite would not resolve either.

**The replacement oracle (after C-03 killed golden vectors):**

1. **Differential testing** — compile pre-fix and post-fix logic into one native binary and
   assert they agree. A true before/after comparison needing no external oracle.
2. **A real capture path** — DONE (2026-08-25). `captureFrame()`/`captureDrain()`
   (`src/capture_functions.cpp`) dump _accepted_ frames as raw bytes, RX under
   `--loradebug on`, TX under the new `--txcapture on`. No longer a compile gate.
3. **Hand-authored specification vectors** — written from the spec, not from the decoder's
   output. Only these can catch a _pre-existing_ bug; the other two are regression fences.

This distinction is now enforced in practice: the first test suite derives its expected
values from the callsign scheme and the code's documented special cases, never from the
function under test.

### 2.4 Delivery shape

Every item is **one commit with evidence in the message**, ready to become one upstream PR.
Commit bodies are German because they become PR descriptions (project rule); the
documentation set is English.

Each fix commit must carry: the mechanism, a concrete failing input, the verification
performed, and RAM/flash delta.

### 2.5 Definition of Done — per fix

A fix is not done until **all** of these hold. This is the operational form of
"zero tolerance" and "don't assume, show it".

| #   | Requirement                                                                                                                                                             |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | The finding is **re-verified against the current tree** — file:line confirmed, not remembered                                                                           |
| 2   | The failure is **demonstrated**, not argued: a concrete input, a computed bound, or a native repro                                                                      |
| 3   | A regression test exists that **fails before and passes after** — or, where no test is yet possible, the demonstration from (2) is recorded verbatim in the commit body |
| 4   | `pio test -e native` is green                                                                                                                                           |
| 5   | Every **affected** environment builds (both MCU families if the file is shared)                                                                                         |
| 6   | RAM/flash delta recorded against the pre-fix baseline                                                                                                                   |
| 7   | Commit body in German: mechanism, triggering input, verification, delta, `Refs:` to the finding ID                                                                      |
| 8   | **One finding per commit.** If you touched two, split them.                                                                                                             |

For anything non-trivial, add: an adversarial pass that tries to **refute** the fix before
it is committed.

### 2.6 Commands

```bash
# Tests (fast, no hardware)
pio test -e native

# Build a target / both MCU families
pio run -e heltec_wifi_lora_32_V3
pio run -e heltec_wifi_lora_32_V3 -e wiscore_rak4631
pio run                                   # all 32 default_envs

# Which environments exist, and what flags does one really get
pio project config --json-output
pio run -e <env> -v | grep -- '-D BOARD_'

# Static analysis (cppcheck ships with the toolchain)
pio check -e heltec_wifi_lora_32_V3

# Structure metrics behind docs 01/04
python3 tools/arch_metrics.py src
python3 tools/arch_duplication.py src 12

# RAM/flash snapshot (note: hardcodes 7 targets — see 08 C-12)
python3 tools/ram_snapshot.py
```

Project skills, in `.claude/commands/`:

| Skill              | Use                                                             |
| ------------------ | --------------------------------------------------------------- |
| `/rebase-upstream` | the documented rebase procedure, incl. post-rebase fixup checks |
| `/build-firmware`  | build the 7 main targets and copy to Desktop                    |
| `/flash-rak`       | build, convert and flash the RAK4631 via UF2                    |
| `/ram-snapshot`    | RAM/flash comparison doc                                        |
| `/code-audit`      | audit against `docs/codequality-rules.md`                       |
| `/submit-pr`       | draft + submit a PR to upstream **DEV** (German description)    |
| `/logauswertung`   | analyse captured serial logs                                    |

### 2.7 Upstream PR mechanics

- Target is the **`DEV` branch** of `icssw-org/MeshCom-Firmware` — never `master`.
- The PR description **must be German** and detailed: which code changed, and why.
- **No PR is currently open.** 24 PRs from this author have been merged historically,
  ranging from `+1 −0` to `+3984 −1516`, so small surgical PRs are the proven path.
- Use `/submit-pr --dry-run` first to review the drafted description.
- Local history rewriting (rebase, commit splitting) requires a force-push to `origin`;
  always `--force-with-lease`, never `--force`.

---

## 3. Steps

### 3.1 Done

| Step                            | Commit     | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| ------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Structural analysis (G1, G2)    | `4a18ae82` | docs 01–07, two reproducible metric scripts in `tools/`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Dependency inventory (G4)       | `4a18ae82` | doc 03 — every "latest" checked against upstream release tags                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| Rewrite decision (G5)           | `4a18ae82` | doc 05                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| **Adversarial review**          | `4a18ae82` | 8 independent finders → verification → doc 08; every decision-relevant claim re-derived by the orchestrator                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| Concurrency / core map (G8)     | see §4     | [`09-concurrency-map.md`](architecture/09-concurrency-map.md) — **13 correctness-affecting races, 9 benign, 5 correctly protected, 16 over-synchronised, 3 dead**. Supersedes the earlier 9/4/14 figures.                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Buffer/type audit (G7)          | see §4     | [`10-buffer-inventory.md`](architecture/10-buffer-inventory.md) — 2 critical, 3 high, 1 medium-high, 11 medium, 12 low(-medium), 2 already fixed; type findings T3-1…T3-9                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Rebase onto `upstream/dev` (G9) | —          | 35 upstream commits integrated, 30 of ours preserved, 0 behind; only conflict was `FLASH_VERSION` (upstream newer, ours dropped as empty)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **SEC-02** format string        | `1cbcf8c9` | native before/after repro; both MCU families built; RAM unchanged, flash +8 B / +16 B                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **N-03** stack overflow         | `93bb68d0` | index bounds computed per packet size; 3 nRF52 targets built                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **TEST-38** CI build gate       | `399d6522` | YAML parsed, env extraction and report filter run locally; release workflow left untouched                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| **TEST-37/39** native harness   | `3fb2c917` | 11/11 green **plus mutation probe** proving the suite can fail                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **N-08** millis() rollover      | `0ccebe8d` | 25 deadline comparisons (21 found + 4 in reversed form) converted to the safe subtraction idiom; native regression test added; 4 boards built, RAM unchanged, flash +16..+64 B                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **N-13** over-sync (partial)    | `4a250602` | 3 of 14 candidates fixed (`scanFlag` deleted, `displayMux` dropped on ESP32, `ch_util_rx_start` platform-split); 4 boards built, ESP32 RAM -16 B / flash -108..-120 B; remaining ~10 candidates deferred                                                                                                                                                                                                                                                                                                                                                                                                                    |
| **N-13** over-sync (rest)       | `d66683d3` | 4 more candidates fixed (`is_receiving`, `ch_util_tx_start`/`rx_accum`/`tx_accum` platform-split); 2 confirmed not real defects (`transmissionState`, `pendingDisplay*`); original 14-item list now closed                                                                                                                                                                                                                                                                                                                                                                                                                  |
| **SEC-03** BLE 0x55 OOB read    | `c342f07e` | `ssid_len`/`pwd_len` bounded against declared frame length, VLAs replaced with fixed buffers                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **BUG-07** BLE 0xA0 underflow   | `e69f88f5` | `msg_len < 2` gate before subtraction                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **SEC-04** URL-decode overrun   | `5d2cf889` | loop redriven by real source consumption, every destination write bounded                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **SEC-05/06/BUG-12** UDP OOB    | `f368a7c3` | read capped at `UDP_TX_BUF_SIZE-1`, zero-scan loop bound fixed; one commit per prior verdict's own grouping                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| **BUG-10** handleACK gate       | `7b968884` | `size < 12` check before the 12-byte `memcpy`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| **BUG-13** APRS trailer bound   | `0aa28f42` | `inext+4 > rsize` check added, matches existing malformed-frame handling                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| **CONC-14** nRF52 BLE queue     | `a441ece6` | BLE callback now enqueues instead of calling `readPhoneCommand()` inline, mirrors ESP32's `bleQueue` exactly; `CONC-15`/`16`/`17`/`18` not re-verified as resolved                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| **BUG-11** `{mcp}` truncation   | `22e84a09` | `cnewMsg[10]` → `[64]`, local `{mcp}` reformat no longer silently truncates the command/password                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **CONC-19** net_console mutex   | `8faee027` | `stopNetConsole()` no longer recreates the live mutex; takes it before teardown instead, matching the file's existing pattern                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| **N-17** boot-time WDT trip     | `6c084c83` | new finding, found flashing real hardware: `startNetwork()`'s blocking WiFi scan ran before the first watchdog feed, boot-looped a gateway-configured Heltec V3; `esp_task_wdt_reset()` added at 4 points; reproduced and fixed on real hardware, pre-existing, unrelated to today's other fixes                                                                                                                                                                                                                                                                                                                            |
| **N-18** BLE-Verbindungsaufbau  | `bd10b636` | **Regression aus unserem eigenen SEC-02-Fix** (`d36fb66f`), auf Hardware per Bisect gefunden: `Serial.printf("%s", temp)` laesst `Print::printf` bei fast jeder Log-Zeile `malloc()`en (64-Byte-Stackpuffer); der Heap-Churn liess NimBLE (`MSYS1_BLOCK_COUNT=4`) keine mbufs fuer den Verbindungsaufbau finden, der Node beantwortete `CONNECT_IND` nicht mehr (Central: `0x3e` / `le-connection-abort-by-local`, auf dem Node feuerte weder `onConnect` noch `onDisconnect`). Fix: `Serial.write()` — SEC-02-Eigenschaft bleibt, ohne Allokation. Bisect gegen **pristine `upstream/dev`**, nicht gegen den Session-Start |
| **BATT-01** Loop-Stall          | `b44fe712` | `read_batt()` blockierte die Hauptschleife ~100 ms alle ~500 ms (`delay(100)` fuer die ADC-Teiler-Einschwingzeit, Heltec-V3/V4/Stick-Zweig); auf Hardware gemessen, jetzt ueber zwei Aufrufe verteilt statt blockierend. Unabhaengig von N-18 und **nicht** dessen Ursache                                                                                                                                                                                                                                                                                                                                                  |
| **0.2** `-Werror` fuer `src/`   | `deba2ad8` | `-Wall -Wextra` galt schon; 3 `src/`-Warnungen behoben (`Regexp.cpp` `-Wclobbered` via `volatile`, `net_console.cpp` `-Wmisleading-indentation`), dann `-Wformat=2 -Wno-missing-field-initializers -Werror` auf `build_src_flags` im `[esp32]`-Block. Reichweite sind die 23 Envs mit `extends = esp32`, alle 23 SUCCESS. Nicht erfasst: die fuenf ESP32-Envs ohne `extends` (`t_deck_pro`, `t5_epaper`, `vision-master-e213`/`e290`, `wireless-paper`) -- dort fehlt schon `-Wall -Wextra` -- sowie die drei nRF52-Boards. `esp32-safeboot` erbt die Flags nicht und ist vorbestehend defekt                               |

**Verification discipline applied throughout** — worth recording, because it caught real
errors in our own work:

- A claim that two shipped environments failed to build was **refuted** by an actual build
  (PlatformIO strips the quotes the manual repro had assumed). The real defect turned out
  to be different and more interesting: a board guard whose truth value depends on
  arithmetic over the product name (`N-10`).
- A finder's "error-severity overflow" was **downgraded** after tracing the source array to
  16 bytes.
- The test suite was mutation-probed rather than trusted for being green.

### 3.2 Immediately next — Wave 0, no hardware needed

| #          | Step                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | Refs            |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- |
| ~~0.4~~    | **DONE 2026-08-20** — `nordicnrf52` auf `10.12.0` gepinnt (alle drei nRF52-Boards: `wiscore_rak4631`, `heltec_t114`, `t_echo`); vorher `platform = nordicnrf52` ohne Version, lokal waren 10.12.0 und 10.3.0 gleichzeitig installiert                                                                                                                                                                                                                                                                                             | doc 02 B-04     |
| 0.2 (Rest) | **wiscore_rak4631 DONE 2026-08-20** (`-Wformat=2 -Wno-unused-parameter -Werror` auf `build_src_flags`, 20 echte src/-Warnungen vorher behoben). `heltec_t114`/`t_echo` bewusst nicht mit angefasst — deckte `CFG-01` auf (`[nrf52]`-Sektionskollision, siehe unten), nicht ohne angeschlossene Hardware fuer diese zwei Boards sauber verifizierbar. Weiterhin offen: die Envs ohne `extends = esp32`, denen schon `-Wall -Wextra` fehlt: `t_deck_pro`, `t5_epaper`, `vision-master-e213`, `vision-master-e290`, `wireless-paper` | doc 08 Wave 0.2 |
| ~~0.3~~    | **DONE** 2026-08-18 — zwoelf Guards auf `defined()` (zwei davon nur ueber `-Wundef` gefunden, `aht20.cpp` ist ISO-8859 und wird von `grep` ohne `-a` uebersprungen). `-Wundef` NICHT im Build aktiviert: 10.317 Treffer, davon 2 aus `src/`. Regressionsschutz stattdessen als CI-Job `macro-guards`. `BOARD_*`-Stringmakros bewusst unangetastet.                                                                                                                                                                                | `N-10`          |
| 0.6        | Extend the native suite: CSMA timing math, `via_functions`, `compress_functions` (the latter already surfaces two `-Wsign-compare` warnings)                                                                                                                                                                                                                                                                                                                                                                                      | doc 08 Wave 0.5 |

The measured warning volume where the flags _are_ active is **9 in the whole build, 4 in
`src/`** — so `-Werror` is nearly free. A June audit rejected it as CRITICAL on the
assumption of a large existing backlog; that backlog does not exist.

### 3.3 Then — Wave 1, RF-reachable criticals

Each one standalone commit and PR. All verified against the source; details in doc 08 §2.

| ID         | Item                                                                       | Size |
| ---------- | -------------------------------------------------------------------------- | ---- |
| ~~`N-01`~~ | ACCEPTED / WONTFIX 2026-08-18 — risk accepted, not fixed                   | —    |
| ~~`N-02`~~ | ACCEPTED / WONTFIX 2026-08-18 — risk accepted, not fixed                   | —    |
| ~~`N-04`~~ | **FIXED** 2026-08-18 — branch-aware producer clamp + consumer bounds check | done |
| ~~`N-05`~~ | **FIXED** 2026-08-18 — both memcpy lengths now source-derived              | done |
| ~~`N-06`~~ | **FIXED** 2026-08-18 — bank/digit validation at all three sites            | done |
| ~~`N-07`~~ | ACCEPTED / WONTFIX 2026-08-18 — bonding would break the app fleet          | —    |

### 3.35 Track B — ESP32/Heltec-V3-Durchgang (2026-08-19)

Alles, was sich am angeschlossenen Heltec V3 umsetzen und verifizieren liess, ist
abgearbeitet — je ein Commit, je gebaut, geflasht und auf Hardware geprueft
(kein Boot-Loop, Webserver HTTP 200, BLE verbindet):

- ~~`SIMP-29`~~ tote Dateien aus `src/` entfernt (`00ab394b`)
- ~~`SIMP-30`~~ doppeltes extern + HDOP-Zwillinge — **echter Defekt**, Display und
  Webseite lasen verschiedene Quellen (`f85b3d7f`)
- ~~`DRY-25`~~ I2C-Bus-Reset-Guard zentralisiert — **echter Defekt**, `BOARD_E22_S3`
  fehlte an den zwei Stellen, die den Sensor adressieren (`741f9af4`)
- ~~`ALT-33`~~ byte-identische Ringgroessen-Zweige zusammengelegt (`627527e6`)
- ~~`ALT-34`~~ `DEFAULT_CALL`/`isNodeUnconfigured()` einmal zentral (`bfc61e28`)
- ~~`ALT-35`~~ `bDisplayDirty` von `bOneButton` getrennt (`10d40be0`)
- ~~`iWrite`/`iRead`/`loraWrite`~~ N-13-Klasse auf ESP32 entatomisiert, Ring unter
  Last geprueft (`26304b1e`)
- ~~`BUG-09`~~ war bereits mit `4e5ef591` behoben, nur nie vermerkt

`STATE-28`s gemeldetes Live-Beispiel ist **refuted** — beide Ausgaben lesen den
Bool, es gibt keine widerspruechliche Anzeige; das Wiederanspringen des Gateways
ist absichtserhaltend. Der Epic-Teil bleibt offen.

Bewusst nicht angefasst, weil Epics und gegen die Projektregel "minimal changes":
`SIMP-26`, `SIMP-27`, `DRY-20`, `DRY-23`, `DRY-24`, `ALT-31`, `ALT-32`.
`DRY-21`/`DRY-22` sind nRF52-seitig. Details: STATUS-Box Wave 3 in doc 08.

### 3.4 Then — Wave 2, remaining prior-verdict Track A

`CONC-15`–`CONC-18`, `N-14`–`N-16` — **auf ESP32 alle sieben einzeln nachgeprueft und
geschlossen (2026-08-18), ohne Codeaenderung; auf nRF52 alle offen.**

Der Katalog verlangte hier ausdruecklich, `CONC-15`/`16`/`17`/`18` nicht auf die Behauptung
des Vorgutachtens hin abzuhaken ("resolved at the root by CONC-14"), sondern einzeln zu
pruefen. Ergebnis: auf ESP32 ist keiner ein Defekt — aber aus einem anderen Grund als
behauptet. `CONC-14` war ein nRF52-Fix und hat auf ESP32 nichts geaendert; tragend ist
`C-01`. Der gemeldete Mechanismus ("Radio-Task rennt gegen Loop-Task") existiert auf ESP32
nicht: `OnRxDone` laeuft synchron in `loopTask`, die einzige weitere Task im Heltec-V3-Build
(`authTask`) fasst keinen Ring an, die ISRs nur `receiveFlag`/`transmittedFlag`, die
NimBLE-Callbacks nur `xQueueSend`, und der Webserver ist ein gepollter `WiFiServer` ohne
eigene Task. Alle Schreiber und Leser der Phone- und UDP-Ringe liegen damit in derselben
Task. `N-14`/`N-16` sind nRF52-only, `N-15` sagt im eigenen Befundtext "True on ESP32".

Vollstaendiger Nachweis inkl. Gueltigkeitsbedingung (was den Befund wieder oeffnen wuerde):
STATUS-Box unter Wave 2 in [`08-defect-catalogue.md`](architecture/08-defect-catalogue.md).
**Damit ist Wave 2 fuer ESP32 leer** — der Rest wartet auf angeschlossene nRF52-Hardware.

~~`N-08`~~ **FIXED** 2026-08-18 — 25 deadline comparisons converted to the safe subtraction
idiom (`0ccebe8d`).

~~`N-09`~~ **CORRECTED, no live hazard** 2026-08-18 — all 11 `while(true);` sit inside a
`/* ... */` block comment that has never been active since the file's creation (Aug 2025);
`lora_init()` unconditionally returns true and its result is never read. No code change; see
the STATUS box on N-09 in `08-defect-catalogue.md`.

~~`SEC-03`~~ **FIXED** 2026-08-18 — BLE 0x55 Wi-Fi config: `ssid_len`/`pwd_len` bounded against
the declared frame length, VLAs replaced with fixed buffers (`c342f07e`).

~~`SEC-04`~~ **FIXED** 2026-08-18 — URL-decode loop into `msg_text_checked[200]` rewritten to
be driven by real source consumption, every destination write bounded (`5d2cf889`).

~~`SEC-05`~~/~~`SEC-06`~~/~~`BUG-12`~~ **FIXED** 2026-08-18 — UDP/external-UDP receive
off-by-one and zero-scan over-read, one combined commit per the prior verdict's own grouping
(`f368a7c3`).

~~`BUG-07`~~ **FIXED** 2026-08-18 — BLE 0xA0 text-command length underflow gated on
`msg_len < 2` (`e69f88f5`).

~~`BUG-10`~~ **FIXED** 2026-08-18 — `handleACK` gated on `size < 12` before the 12-byte
`memcpy` (`7b968884`).

~~`BUG-13`~~ **FIXED** 2026-08-18 — APRS trailer/FCS read bounded against `rsize`, matching
the existing malformed-frame convention (`0aa28f42`).

~~`CONC-14`~~ **FIXED** 2026-08-18 — nRF52 BLE receive callback now enqueues instead of
calling `readPhoneCommand()` inline, mirroring ESP32's `bleQueue` design exactly (`a441ece6`).
`CONC-15`/`16`/`17`/`18` were **not** re-verified as resolved by this root fix — treat as
still open until checked individually.

~~`BUG-11`~~ **FIXED** 2026-08-18 — `cnewMsg[10]` → `[64]`, local `{mcp}` reformat no longer
silently truncates the command/password (`22e84a09`).

~~`CONC-19`~~ **FIXED** 2026-08-18 — `net_console.cpp`'s `stopNetConsole()` no longer
recreates the live mutex without holding it; takes the existing mutex before teardown instead,
matching the file's own established pattern (`8faee027`).

### 3.5 Then — Wave 3, structural (propose upstream as a plan first)

~~`N-13`~~ **FULLY RESOLVED** 2026-08-18 — first pass (`4a250602`): `scanFlag` deleted,
`displayMux` dropped on ESP32, `ch_util_rx_start` platform-split. Second pass (`d66683d3`):
`is_receiving`, `ch_util_tx_start`/`rx_accum`/`tx_accum` platform-split. `transmissionState`
and the `pendingDisplay*` fields confirmed **not actual defects** (plain `volatile`, never
atomic; already resolved by the first pass's `displayMux` removal, respectively) — the
original 14-item list is closed. `iWrite`/`iRead`/`loraWrite` surfaced as further same-class
candidates during re-verification but were never part of this list — deliberately left open,
not yet scoped.

`DRY-20`–`DRY-25`, `SIMP-26`–`SIMP-30`, `ALT-31`–`ALT-35`, `STATE-28`, and the corrected C-02
extraction of the ~221 genuinely shared, radio-independent loop lines.

### 3.6 Deferred, with explicit triggers

| Item                                                                                     | Revisit when                                                                              |
| ---------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| Arduino 3.x migration                                                                    | Wave 0 has delivered a RAM baseline and a CI gate                                         |
| Arduino 2.0.14 → 2.0.17 on the 4 lagging boards                                          | the CI matrix is in place                                                                 |
| Radio interface / HAL                                                                    | only after C-02's cheap extraction proves the seam                                        |
| LVGL 8 → 9                                                                               | never, unless the T-Deck UI is rewritten anyway                                           |
| ~~Licensing (`N-11`)~~ — **ACCEPTED** 2026-08-18, risk accepted, no fix planned          | closed                                                                                    |
| `FLASH_VERSION` migration (`N-12`) — re-verified 2026-08-18, still deferred (see doc 08) | **before** any change to the `meshcom_settings` layout                                    |
| Hardware bench (2 × Heltec V3)                                                           | after the no-hardware steps; see doc 07 for wiring, frequency plan and scenario catalogue |

### 3.6a Sizing — input for the G12 decision

Deliberately coarse. A "session" is one focused working block including verification per
§2.5, not calendar time. Ranges are wide where the work is discovery-heavy.

| Block                                    | Items | Estimate       | Hardware | Notes                                                                |
| ---------------------------------------- | ----: | -------------- | -------- | -------------------------------------------------------------------- |
| Wave 0 — enablement                      |     4 | 2–4 sessions   | no       | 0.4 is minutes; 0.2 unknown until the 9 envs are built with warnings |
| Wave 1 — RF criticals                    |     6 | 4–8 sessions   | no       | each small in code, but each needs a test and an adversarial pass    |
| Wave 2 — remaining Track A               |   ~20 | 12–20 sessions | partly   | `CONC-14` is the root fix for four others; BLE items want a phone    |
| Wave 3 — structural                      |   ~17 | 20–40 sessions | partly   | propose upstream as a plan first; several are epics, not fixes       |
| Hardware bench bring-up                  |     — | 2–4 sessions   | **yes**  | wiring, attenuator, frequency plan, first scenarios                  |
| Wire-format specification (still absent) |     — | 3–5 sessions   | no       | prerequisite for spec-derived test vectors (§2.3 item 3)             |

Two honest caveats. **The Wave 2 and 3 numbers are the least trustworthy** — they come from
finding counts, not from having opened each one. And **Wave 0.2 could be anything**: nine
environments have never been compiled with warnings enabled, so the backlog behind them is
literally unmeasured. Build those nine first; the number that comes out should drive the
G12 decision more than any estimate here.

What this suggests: Wave 0 and Wave 1 are a coherent first push (~6–12 sessions, no
hardware, ends with the six criticals fixed and a gate that keeps them fixed). That is the
natural unit to decide about, rather than "everything".

### 3.7 Open decisions

| #   | Decision                                                                                                                                                                            | Status                                                      |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| G12 | All at once, or spread over weeks?                                                                                                                                                  | **open** — sizing now possible from doc 08 §5               |
| —   | Split the 5 historical bundled commits into one-per-finding? (`e957b964` = D1+D2+D5+A1, `b2854586` = A1+B1+B5+C2, `4123eac0` = A1+A2+B2+C3, `3457a295` = A1+D3, `8009aa19` = C1+B3) | **open** — needed only if those go upstream as separate PRs |
| —   | When to open the first upstream PRs                                                                                                                                                 | **open**                                                    |

### 3.8 Tooling backlog — `loganalyse.sh` / `logauswertung` skill (found 2026-08-24)

Found while running `/logauswertung` on a 3-node DG0OPK mesh test (DG0OPK-11/-12/-13, ~29.5 h each).
These are **analysis-tooling** defects — they corrupt the _conclusions_ drawn from a log, not firmware
behaviour. The run initially produced a **false FEHLER verdict** on the State Machine and a **misleading
priority-drop breakdown**, and silently truncated the first pass at ~71 % of the file. Full evidence,
root cause, fix, and verification per item: [`bug-loganalyse-toolchain-20260824.md`](bug-loganalyse-toolchain-20260824.md).

| ID      | Type        | Sev.   | Location                                        | Item                                                                          | Status     |
| ------- | ----------- | ------ | ----------------------------------------------- | ----------------------------------------------------------------------------- | ---------- |
| TOOL-01 | BUG         | Medium | `tools/loganalyse.sh:401`                       | CSMA backoff (`TX_PREPARE→IDLE rc=-1`) counted as State-Machine errors        | `893ba656` |
| TOOL-02 | BUG         | Medium | `tools/loganalyse.sh:1006`                      | Priority-drop breakdown double-counts via `replaced_by_prio=` substring       | `a67bf6cb` |
| TOOL-03 | BUG         | Low    | `tools/loganalyse.sh:182,185,188`               | Hop regex `H[0-9]{2}` matches telemetry payload (`H19/B=…`)                   | `9226b65c` |
| TOOL-04 | BUG         | High   | `tools/loganalyse.sh` (all awk sections)        | One corrupt byte aborts awk mid-file → sections silently truncated (no error) | `5f51627e` |
| TOOL-05 | ENHANCEMENT | Medium | `tools/loganalyse.sh` (arg handling)            | Auto-detect + convert raw firmware bracket timestamp format                   | `4bdf27a6` |
| TOOL-06 | DOC         | Low    | `.claude/skills/logauswertung/SKILL.md` §12,§15 | "any rc!=0 is a bug" is wrong; align with TOOL-01/02                          | local\*    |

**DONE 2026-08-24** via `/orchestrate-waves` — Wave 1 (TOOL-01/02/03 + harness, three Sonnet writers),
Wave 2 (TOOL-04/05, orchestrator-direct: pure-awk Gregorian date math). Each fix carries a regression
test (red before, green after); suite `python3 -m unittest discover -s tools/mock -p 'test_loganalyse.py'`
→ 5/5. End-to-end re-verified on the real raw DG0OPK log. Full record + deviations: the Resolution
section of [`bug-loganalyse-toolchain-20260824.md`](bug-loganalyse-toolchain-20260824.md).

\*TOOL-06 edits the git-ignored `.claude/…/SKILL.md` (`.gitignore` rule `.*`) — applied on disk, no
commit. CI wiring was deliberately deferred (test is local-only). The `git add -p`-based per-id split
of the shared harness was not possible (interactive staging disabled), so the harness is one commit
(`b7a54f2f`) after the three fixes it validates.

### 3.8a Headless configurability — GUI-only settings block automated tests (found 2026-08-28)

Found while bringing a **T-Deck Plus** (`DK5EN-14`, ESP32-S3 16 MB / 8 MB PSRAM) up from a virgin
flash over USB serial. Goal: every setting a node needs to reach a usable state must be reachable
**over USB serial alone**, so hardware tests can be automated without a human touching the display.

Callsign, SSID and password all set fine over serial. The node still refused to go online —
`[WIFI]...disabled by Settings (node_wifion=false)` — and no serial command can clear that flag.
Enabling WLAN on a T-Deck currently requires physically tapping an unlabelled icon button in the
Setup tab.

**Correction (2026-08-28, later): this is a T-Deck-only defect, not a general headless one.** The
gate in `udp_functions.cpp:545` sits inside `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`,
so on every other board `node_wifion` is never consulted and SSID + password alone bring the node
online. Verified by provisioning a Heltec V3 (`DK5EN-93`) over serial only: it reached DHCP without
any GUI interaction. The earlier claim that "any board without that GUI" is affected was wrong.

| ID    | Type | Sev.   | Location                                                  | Item                                                                                                                                                                                                                                | Status |
| ----- | ---- | ------ | --------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ |
| HL-01 | GAP  | High   | `t-deck/event_functions.cpp:277`; `udp_functions.cpp:545` | `node_wifion` is assigned only in the `btn_wifi` GUI handler, and the gate that reads it is `#if`-guarded T-Deck-only. No `--wifi on/off`, so a serially provisioned **T-Deck** can never join a WLAN; other boards are unaffected. | open   |
| HL-02 | BUG  | Medium | `esp32/esp32_flash.h:239` vs `esp32/esp32_flash.cpp:204`  | Struct default is `node_wifion = true`, but the load uses `preferences.getBool("node_wifion", false)`. On virgin NVS the load default wins → every freshly flashed node boots with WLAN off.                                        | open   |
| HL-03 | GAP  | Low    | `t-deck/event_functions.cpp` (`btn_soundon`)              | `audio_set_mute()` has no serial equivalent.                                                                                                                                                                                        | open   |
| HL-04 | GAP  | Low    | `t-deck/event_functions.cpp` (`btn_persist_*`)            | `node_persist_to_flash`, `node_persist_to_sd`, `node_immediate_save` are GUI-only.                                                                                                                                                  | open   |

**Verified as already serial-complete** (checked handler-by-handler against the command table, so the
fix does not need to touch them): `btn_gps` → `--gps on/off`, `btn_track` → `--track on/off`,
`btn_webserver` → `--webserver on/off`, `btn_wifiap` → `--wifiap on/off`, `btn_mesh` → `--mesh on/off`,
`btn_noallmsg` → `--nomsgall on/off`. Each GUI handler literally calls `commandAction()` with that
string.

GPS round-trip proven on hardware over serial — `--gps off` → `--pos` reports `...GPS: off`,
`--gps on` → `...GPS: on`. So **only WLAN is missing**, not "WLAN and GPS".

**Acceptance for HL-01:** with the device freshly flashed and reachable only over
`/dev/cu.usbmodem*`, this sequence must bring it online with no display interaction:

```
--setcall <call>
--setssid <ssid>
--setpwd <pwd>
--wifi on
```

`--info` must then report `hasIpAddress: yes`. The `btn_wifi` handler must be reduced to calling
that same command, so GUI and serial cannot drift apart — the pattern `btn_gps` and `btn_mesh`
already follow.

---

### 3.8b T-Deck Plus bring-up — open issues (`DK5EN-14`, from 2026-08-28)

**In progress.** Device handed over by the upstream FW maintainer, who was tracking a heap defect
on the T-Deck at the time:

> "Ich suche gerade im T-DECK seit zwei tagen einen HEAP Fehler .. und heute auch ohne KI gefunden.
> Das Hirn muss immer trainiert werden" — "wenn ein fehler bekannt ist kann man dass schon
> reparieren. Die versteckten fehler sind das Thema"

So the target is not the known defect but the **class** of defect: what else in the T-Deck GUI path
has the same shape. Bring-up record and serial quirks: §3.8a.

| ID    | Type  | Sev.   | Location                                                                                                   | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Status                     |
| ----- | ----- | ------ | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------- |
| TD-01 | BUG   | High   | `udp_functions.cpp:629-698`                                                                                | WLAN association fails on ~50 % of boots; BSSID pinning prevents fallback to the 2nd AP                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | open                       |
| TD-02 | NOFIX | —      | —                                                                                                          | "Constant reboots" not reproducible — caused by the host opening the USB port                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | closed                     |
| TD-03 | BUG   | High   | `lv_obj_functions.cpp` `msg_tabs_add_message()` fast path (H1), `esp32_audio.cpp` (C2, fixed in TM-01..04) | Heap defect: rendered message list of the active tab never trimmed while the model is capped at 50 (PSRAM ~2.3-2.8 kB per message, unbounded). **FIXED 2026-08-29:** `msg_list_trim_view()` deletes the oldest wrapper past `MSG_TAB_MAX_MESSAGES` on the fast path (`lv_obj_del` fires `LV_EVENT_DELETE`, freeing `HeaderEventData`/`DeleteEventData`). Harness scenario `trim` (60 injections on the open tab): before-fix arm 60 children / PSRAM -160 500 B, fixed 50 / -137 592 B; 20 further messages on the saturated view cost -584 B PSRAM in total. | **FIXED** — harness `trim` |
| TD-04 | TASK  | Medium | `tdeck_sdmap.cpp`; SD card                                                                                 | Install Europe map tiles from `download.tiles.coalition.space`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | in progress                |
| TD-05 | PERF  | Medium | `lv_obj_functions.cpp` (4313 lines)                                                                        | GUI is sluggish — cause unidentified                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | in progress                |
| TD-06 | TEST  | High   | `tools/`                                                                                                   | Automated test harness driving the device over serial + net console                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | in progress                |

#### TD-01 — WLAN association, and the "wrong access point?" question

**It does not pick the wrong AP.** `startNetwork()` scans all channels, selects the strongest RSSI
(`udp_functions.cpp:650`), and that is consistently the nearer Orbi. Measured over four boots:

| Boot | AP `5A:…:8B` | AP `46:…:86` | Chosen | Result                             |
| ---- | ------------ | ------------ | ------ | ---------------------------------- |
| 1    | −72 dBm      | −94 dBm      | `5A`   | connect OK, `192.168.68.71`        |
| 2    | −78 dBm      | −93 dBm      | `5A`   | 6× `connection error`, radio reset |
| 3    | −74 dBm      | (n/a)        | `5A`   | connect OK, `192.168.68.71`        |
| 4    | −74 dBm      | −88 dBm      | `5A`   | 6× error, reset, 6× error, gave up |

Two findings instead:

1. **The association fails at a signal level that should work.** Six retries at −73/−74 dBm, twice.
   Not a range problem in the usual sense.
2. **BSSID pinning removes the fallback.** `WiFi.begin(ssid, pwd, channel, BSSID, true)`
   (`udp_functions.cpp:696-698`) locks onto one BSSID. When that AP refuses, the supplicant cannot
   try the second Orbi — even though both carry the same SSID. On failure the node waits **5 minutes**
   before the next attempt. Softening the pin to SSID-only after the first failed pass is the
   obvious mitigation.

Also noted: both APs advertise **channel 3** — co-channel, same SSID. And the recovery path emits
`E (…) wifi:timeout when WiFi un-init, type=4` from the IDF during "full radio reset", i.e. the
reset path itself is not clean.

**Operator answers (2026-08-28), which narrow this considerably:**

- Orbi runs **WPA2/WPA3 Personal**, 2.4 + 5 GHz, 80 MHz channel width.
- **`DK5EN-98` — a Heltec V3 — sits on the same SSID and connects without trouble** (verified live:
  `192.168.68.56`, `hasIpAddress: yes`, net console reachable on 2323).

Same SoC family, same firmware version, same `startNetwork()` code path, same AP, same security
mode. So **WPA2/WPA3 transition is not the sole cause** — if it were, the Heltec would fail too.
That points at something T-Deck-specific: RF (antenna, or self-interference from display backlight,
OPI PSRAM and audio amp) or boot-time ordering, not the association logic as such.

**Refinement:** the node is _not_ permanently offline. `192.168.68.71` answered ping some minutes
after the capture ended, so the 5-minute retry does succeed. The correct statement of the defect is
therefore **"association fails during boot and only recovers on a later retry"**, not "cannot
connect". That also means the observable symptom is a node that is silently absent from the network
for the first ~5 minutes after every power-on — bad for a headless test rig, and the reason TD-06
needs it fixed.

Next step: log RSSI side by side on both nodes at the same location, to separate "T-Deck hears
worse" from "T-Deck associates worse".

#### TD-01 update (2026-08-28, later) — reproduced on a second board; not RF, not T-Deck-specific

A Heltec V3 (`DK5EN-93`, ESP32-S3, 8 MB flash, no PSRAM) was flashed with the same firmware and put
on the same SSID, at the same location. It reproduces the defect exactly:

```
[WIFI]...SSID: ORBI63 CHAN: 3 RSSI: -47 BSSID: 5A:AF:97:2E:2B:8B
[WIFI]...power: 80 RSSI:-50
[WIFI]...ssid<ORBI63> connection error      (x6)
[WIFI]...no connection at boot — full radio reset and retrying
[WIFI]...ssid<ORBI63> connection error      (x6)
[WIFI]...SET but no Wifi connect ...please wait for next try (5 min)
   ... 5 minutes later ...
[WIFI]...connect OK
[WIFI]...now listening at IP 192.168.68.66, UDP port 1990
```

**This kills three earlier hypotheses at once:**

- **Not signal strength.** −47 dBm is excellent. Twelve consecutive association failures at that
  level are not a link-budget problem.
- **Not T-Deck RF.** The Heltec sees the same AP **22-28 dB stronger** than the T-Deck does
  (−47/−50 vs −72/−78 — a real and separately interesting hardware difference), and still fails.
- **Not "the T-Deck associates worse".** Same failure, same count, same recovery on the 5-minute
  retry, on a board with no PSRAM, no display and no LVGL.

**Restated defect:** on ESP32 nodes running this firmware against this AP, **boot-time association
fails reliably, and only the delayed retry succeeds**. Every node is therefore absent from the
network for about five minutes after each power-on. That is the real cost, and it is much larger
than "the T-Deck has a bad antenna".

**Leading hypothesis, not yet tested:** BLE/WiFi coexistence. Both share the single 2.4 GHz radio;
NimBLE begins advertising during boot, and the failures cluster in exactly that window while the
successful attempt happens minutes later when advertising has settled. The next experiment is to
disable BLE at boot on one node and see whether the first association then succeeds. Alternative
candidates not yet excluded: the BSSID-pinned `WiFi.begin()` issued immediately after
`WiFi.scanNetworks()`, and WPA2/WPA3-transition PMF negotiation.

**Consequence for TD-06:** an automated test rig cannot assume a node is reachable after boot. Either
the defect is fixed, or the harness waits out the 5-minute retry on every power cycle.

#### TD-02 — the reboots were ours

Audible restarts during bring-up were **not** a device fault. The T-Deck Plus uses the ESP32-S3
USB-Serial/JTAG peripheral: **every host port open triggers `rst:0x15 (USB_UART_CHIP_RESET)`**, and
each boot plays the startup tone. Roughly ten tool invocations produced roughly ten audible boots.

Counter-evidence, so this is not chased again: **240 s of uninterrupted serial observation produced
exactly one reset — the one at t=0.3 s from our own connect.** No panic, no `Guru Meditation`, no
watchdog, no brownout, no backtrace. Heap flat across the window (95 936 → 95 884 B free, min-ever
90 252 B, largest block 81 908 B, both `(mon)` samples identical to within 52 B).

To observe without perturbing: enable `--netconsole on` and watch TCP 2323 (`tools/hmac_connect.py`),
which does not reset the device.

#### TD-03 — heap defect

Not yet localised. The 4-minute window above is far too short to prove anything; it only rules out a
fast leak. Needs a long soak with `[HEAP]` sampling (the `(mon)` line already emits free / min-ever /
largest-block) against a device that is actually exercising the GUI, LoRa RX and the SD map path.
`tools/meshlogger.py` can record it for days over the net console.

**Fixed 2026-08-29 (H1).** The model trim (`msg_tabs_trim_history`, 50 entries) had no view
counterpart on the fast path that appends to the already-active tab. `msg_list_trim_view()` now
deletes the oldest wrapper while `lv_obj_get_child_cnt(msg_list) > MSG_TAB_MAX_MESSAGES`; deleting
the wrapper fires `LV_EVENT_DELETE` on the header label and delete button, which is where the
`HeaderEventData`/`DeleteEventData` allocations are freed. Regression: `tdeck_harness.py --scenario
trim` (`--trim-count`, default 60) samples `[UISTAT] msg_list` after every injection and fails if
the child count ever exceeds 50 or the final count is not `min(count, 50)`. Measured on DK5EN-14:
before-fix arm `max_children=60`, PSRAM -160 500 B over 60 messages; fixed `max_children=50`, PSRAM
-137 592 B (that is the 50 bubbles being built from an empty view), and the `heap` scenario's 20
further messages on the saturated view cost -584 B PSRAM / -1 736 B internal in total. The long
soak is no longer needed for H1; keep it as a general watch (meshlogger) once the PR is out.

#### TD-04 — Europe map on SD

Card present and healthy: **SDHC, 30 436 MB**, mounts under MeshCom (the LilyGo factory firmware
failed on the same card, so this was never a card fault). Currently `/maps` is absent and the node
logs `[ SDMAP ]...Ordner /maps nicht gefunden!` plus repeated
`[ SDMAP ]...Keine Kachel fuer diese Position in  gefunden (Zoom 0 bis 0 geprueft)`. Tiles to come
from `https://download.tiles.coalition.space/`. Needs: expected directory layout, tile format and
zoom range that `tdeck_sdmap.cpp` actually reads — verify against the code before downloading a
large tile set.

#### TD-05 — GUI latency

Unquantified so far. `lv_obj_functions.cpp` is 4313 lines and builds the whole widget tree; the LVGL
tick/refresh path, the SD map lookups on the draw path, and `Print::printf` heap churn (see
`printf malloc` note in the campaign memory) are the first three suspects. Measure before changing.

#### TD-06 — automated tests

Serial control is nearly complete (§3.8a); `--wifi on/off` (HL-01) is the missing piece that would
let a test bring a virgin node fully online unattended. Planned instruments: USB serial for
provisioning and assertions, net console 2323 for non-perturbing observation, and a second node
(`DK5EN-98`) to watch what `DK5EN-14` actually transmits.

#### Explicitly out of scope

The 869 MHz band-check defect found during bring-up (empty interval at `LORA_BANDWIDTH 250`,
`command_functions.cpp:4254`) is **not being fixed**: this deployment is 433 MHz only and will never
transmit on 869 MHz. Recorded here so the finding is not rediscovered and mistaken for new.

---

### 3.8c T-Deck GUI review — stage 1 findings (2026-08-28)

`/code-review medium` over `src/t-deck/` against [`codequality-rules.md`](codequality-rules.md),
carrying forward the defect classes from [`code-audit-20260626.md`](code-audit-20260626.md).
**16 findings, 3 HIGH.** Full evidence, reproduction and fix direction:
[`review-tdeck-gui-20260828.md`](review-tdeck-gui-20260828.md).

Stage 2 (`/fable-review`, adversarial verification with advisor gating) still to run — the operator
chose the two-stage route. **No code changed yet, and no regression tests exist for any of these.**

| ID  | Sev.    | Location                    | Item                                                      | Topic | Status       |
| --- | ------- | --------------------------- | --------------------------------------------------------- | ----- | ------------ |
| G01 | HIGH    | `lv_obj_functions.cpp:1777` | Dangling LVGL pointers → use-after-free in PSRAM heap     | TD-03 | verified     |
| G02 | HIGH    | `lv_obj_functions.cpp:1723` | `ic <= MAX_MAP` reads past `strMaps[5]`                   | —     | verified     |
| G03 | HIGH    | `lv_obj_functions.cpp:3570` | Hemisphere signs swapped; S/W stations plotted N/E        | —     | verified     |
| G04 | MEDIUM  | `lv_obj_functions.cpp:183`  | Up to 5 000 Arduino `String` blocks on the internal heap  | TD-03 | verified     |
| G05 | MEDIUM  | `tdeck_main.cpp:600`        | SD I/O + PNG decode + ~870 ms `delay()` in LVGL `read_cb` | TD-05 | not verified |
| G06 | MEDIUM  | `tdeck_main.cpp:262`        | `addMessage()` blocks the main loop 2 s (8 s at boot)     | TD-05 | not verified |
| G07 | MEDIUM  | `tdeck_main.cpp:351`        | Draw-buffer size passed in bytes, not pixels              | —     | not verified |
| G08 | MEDIUM  | `tdeck_main.cpp:337`        | Unchecked `malloc` fallback → NULL deref at boot          | —     | not verified |
| G09 | MEDIUM  | `tdeck_sdmap.cpp:216`       | Unbounded allocation from SD-supplied file size           | TD-04 | not verified |
| G10 | MEDIUM  | `tdeck_sdmap.cpp:52`        | Active set clamped to compile-time, not discovered, count | TD-04 | not verified |
| G11 | MEDIUM  | `tdeck_sdmap.cpp:186`       | Stale tile state suppresses retry after a failed load     | TD-04 | not verified |
| G12 | MEDIUM  | `event_functions.cpp:598`   | `memcmp` reads 49 B past `node_passwd[15]`                | —     | not verified |
| G13 | MEDIUM  | `event_functions.cpp:652`   | Uninitialised `iNewPower` persisted to flash              | —     | not verified |
| G14 | MEDIUM  | `lv_obj_functions.cpp:4290` | Timestamp format mismatch makes clock restore dead code   | —     | not verified |
| G15 | LOW/MED | `tdeck_main.cpp:113`        | Binary semaphore as SPI mutex, `portMAX_DELAY`            | —     | not verified |
| G16 | LOW     | `lv_obj_functions.cpp:3642` | `millis()` wraparound comparison (STAB-05)                | —     | not verified |

**This answers TD-03 with two candidate mechanisms**, both on the internal heap rather than PSRAM —
which is why PSRAM telemetry looked healthy during bring-up while free internal heap sat at ~95 KB.
G01 is a use-after-free whose correct pattern already exists twenty lines further down in the same
function; G04 is unbounded `String` accumulation. Either would match the maintainer's report.

**G09/G10/G11 gate TD-04:** fix them before populating a 30 GB card, since G09 makes every tile
change cost ~256 KB of transient internal heap and G10 can make a correctly populated card look
empty.

---

### 3.8d MHeard `MOD` byte — country nibble collides with the "not from last hop" marker (2026-08-28)

External wire-contract report from DK5EN (MCProxy/McApp side), verified against this tree on
2026-08-28: [`~/WebDev/MCProxy/doc/2026-08-28_1700-firmware-mod-nibble-handover.md`](../../MCProxy/doc/2026-08-28_1700-firmware-mod-nibble-handover.md).
**Two defects, both confirmed at the cited lines.** No code changed yet, no regression test exists.

Not a crash — a data-loss and ambiguity defect in a field that already has downstream consumers
(BLE MHeard register → MCApp, web UI, `--mheard` dump). Low impact today, but the wire cannot
express the difference, so it is silently unfixable downstream. Decide before more consumers read
the field.

| ID    | Sev.   | Location                          | Item                                                               | Status   |
| ----- | ------ | --------------------------------- | ------------------------------------------------------------------ | -------- |
| MH-01 | MEDIUM | `lora_functions.cpp:587`          | `\| 0xF0` marker overwrites the country nibble; `0xF` == `PL` (15) | verified |
| MH-02 | LOW    | `aprs_functions.cpp:126, 448/454` | Absent optional trailing fields keep the **receiver's** hw/fw      | verified |

**MH-01.** `msg_source_mod` packs two nibbles: low = `getMOD()` (range 3..8,
`lora_setchip.cpp:169-191`), high = `node_country` (`aprs_functions.cpp:113`, and identically
`lora_functions.cpp:1232`, `udp_functions.cpp:224`/`:314`, `nrf52/nrf_eth.cpp`). Writing the MHeard
register (`lora_functions.cpp:585-587`), the "modulation did not come from the last hop" marker is
stamped **into the country nibble** by forcing it to `0xF`. But `strCountry[15]` is `"PL"`
(`lora_setchip.cpp:62`) and `--country` accepts 0..15 — it rejects only index 16 `"none"`
(`command_functions.cpp:4163`). Consequences:

- `mh_mod >> 4 == 0xF` is ambiguous — "station is in PL" or "provenance unknown". Nothing on the
  wire separates them.
- Every entry taking the `else` branch **loses the sender's real country permanently**.
- A genuine PL node arrives as `mh_mod = 0xF3` and is indistinguishable from a marked entry.

Reaches: `web_functions.cpp:939`, `mheard_functions.cpp:725` (both already decode the two nibbles),
and BLE `mhdoc["MOD"]` (`mheard_functions.cpp:337`, re-emitted at `:678`). The value also
round-trips through the stored MHeard table, so a marked entry stays marked for the life of the
slot.

_Fix direction (reporter's preference, smallest change):_ mark the **modulation** nibble instead —
`mheardLine.mh_mod = aprsmsg.msg_source_mod & 0xF0;`. `0` is outside the valid `getMOD()` range
3..8, so it reads as "unset" to every existing consumer, and the country survives. Alternative: add
a `bool mh_mod_from_last` to `struct mheardLine` (`aprs_structures.h:78`, internal) and leave
`mh_mod` unmodified — but the BLE register is already tight against `BLE_JSON_PAYLOAD_MAX` (244),
so export a key only if a consumer needs it. There is no spare bit in the byte; both nibbles are
fully used.

**MH-02.** `decodeAPRS` opens with `initAPRS(aprsmsg, 0x00)` (`aprs_functions.cpp:126`), which fills
the struct with **this node's own** identity (`:111-117`), including
`msg_last_hw = 0x80 | BOARD_HARDWARE`. `msg_source_fw_version` and `msg_last_hw` are then overwritten
from the frame **only if those bytes are present** (`:446-456`). A frame ending after the FCS
therefore keeps our own values, so:

1. `mheardLine.mh_hw = aprsmsg.msg_last_hw & 0x7F` (`lora_functions.cpp:582`) records **our own
   board type** as the heard station's hardware.
2. The `0x80` test always passes, so MH-01's marker never fires for exactly the old frames it exists
   to mark.
3. The pre-4.35 discard gate (`aprs_functions.cpp:486`, `> 0 && < 35`) cannot fire — the defaulted
   value is our own version, ≥ 35.

Fully initialised memory, not corruption — a defaulting choice. _Fix direction:_ after `initAPRS` in
`decodeAPRS`, reset the two **optional** fields to the sentinel `0` ("not supplied by sender"); `0`
is already what the fw-version gate expects, and `mh_hw == 0` reads as "unknown hardware" instead of
a wrong board type.

**Order matters:** MH-02 is what makes MH-01's `else` branch reachable at all for legacy frames.
Fixing MH-01 alone leaves the marker mostly dead; fixing MH-02 alone increases how often the country
nibble gets destroyed. Take both in one wave, two commits.

DK5EN offered a before/after on the `MOD` byte from `DK5EN-98` (mcapp.local) — every MHeard frame is
logged there, so verification on hardware is cheap. Per §2.5 a native regression test over
`decodeAPRS` + the MHeard write path is the primary gate.

---

### 3.8e T-Deck GUI review — stage 2 verdict (2026-08-28)

`/fable-review`, seven blind finders + verification. Full document:
[`tdeck-gui-verdict.md`](tdeck-gui-verdict.md). **No code changed.**

**TD-03 (heap) now has a leading candidate, and stage 1's was refuted.**

- **H1** (`lv_obj_functions.cpp:2770`) — the rendered message list is never trimmed while the model
  is. Each broadcast on the **open** group tab costs ~250 B internal + ~1.9 KB PSRAM permanently;
  the ~95 KB internal pool is gone at ~390 messages → `abort()`, with PSRAM still reading healthy.
  **It heals on any tab switch**, which is why it resists interactive debugging.
- **C2** (`esp32_audio.cpp:115`) — a second, independent mechanism on the same pool: the audio
  semaphore is released before playback starts, so two messages inside one sound free the MP3
  decoder buffers under the reading task.
- **G01 refuted as the cause** — real UAF, but PSRAM-only, immediate rather than monotonic, and
  needs a human zooming. Keep the fix, drop the theory. **G04 overstated ~2×** (String SSO).

**TD-05 (sluggish GUI) is answered.** `full_refresh = 1` + single buffer + non-DMA `pushColors` at
27 MHz = **~45 ms blocking SPI on every invalidation**, including the 1 Hz clock tick, holding the
bus that also fronts SD and LoRa. `LV_ANIM_ON` scroll multiplies it to 200-300 ms per message.

**Scope lesson.** The whole concurrency cluster (C1-C5) lives in `src/esp32/esp32_audio.cpp`, so
stage 1's `src/t-deck/` file scope could not reach it — yet that task is spawned by every incoming
mesh text message, and it is created at priority 50 against a `configMAX_PRIORITIES` of 25, silently
clamped to the highest task priority in the system.

**Correction affecting TD-06.** Both fork workflows are `disabled_manually`; `ci-build.yml` does not
exist upstream. The native suites run **nowhere automatically** — every "green" claim in the docs is
a manual local run. `src/t-deck/` has 0 % coverage.

---

### 3.8f Timing campaign — bus contention and loop stalls, all platforms (2026-08-29)

Scouted 2026-08-29 with six read-only agents after the T-Deck lost-flush root cause
(`tdeck-findings-20260828.md` §1). Goal of the campaign: fix every timing defect of the
"shared bus / blocked loop task" class once, across T-Deck Plus, Heltec V3, T-Beam and RAK4631.
**State 2026-08-29 evening:** TM-01..05, 08, 09, 12, 15, 18, 20, 21 done and verified on hardware;
TM-11 measured with a candidate fix; open: TM-06, 07, 10, 13, 14, 16 (rest), 17, 19, 22, 25-34. **TM-20 stays out of the PR** (AP selection, see TM-34). file:line
references are from the morning scouting; re-verify before touching.

#### What the scouting settled

| Lead                            | Verdict                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| T-Deck tone freeze (~1.1 s)     | **Not a bus problem.** `msg_focus_and_alert()` (`lv_obj_functions.cpp:4045-4047`) falls back to `play_cw('r')`, which runs synchronously on `loopTask` (`esp32_audio.cpp:289-477`, `i2s_write` per ms, morse `.-.` = ~1100 ms). I2S is on pins 5/6/7, not SPI2. The only bus touch is `SD.exists()` on SPI2 per message (`esp32_audio.cpp:114`). Fix is task placement, not the NOP mitigation.                                                   |
| T-Deck tone via SD file         | **Is a bus path.** When the tone file exists, `play_file_from_sd()` spawns `play_function` at priority 50 (clamped to 24, `esp32_audio.cpp:139-144`) which streams from SD on SPI2 while `loopTask` flushes the TFT — no shared mutex. This is the C1/C2 cluster from §3.8e and the one audio path that _can_ produce lost flushes.                                                                                                               |
| Heltec V3 slow refresh          | **Always full redraw, on bit-banged I2C.** `loop_functions.cpp:364-365`: V3/V4 (and Stick V3, T-Beam V3) use `U8G2_*_1_SW_I2C` — 1-page buffer, 8 passes of 128 B per frame, software I2C at ~100 kHz, on pins 17/18. No dirty flag, no partial update; pushed on every message and every 5 s status tick. Heltec V2 and RAK use `_F_HW_I2C`. Not shared with sensors (Wire on 41/42); no bus contention — the cost is the transport itself.      |
| T-Deck WiFi association (TD-01) | **Not bus-related.** Reproduced on a Heltec V3 (no SD, no TFT, no LVGL) with identical failure count and recovery. Leading hypothesis unchanged: BLE/WiFi coexistence — NimBLE advertising starts (`esp32_main.cpp:1642-1719`) before `startNetwork()` (`:1752`). Decisive test still not run.                                                                                                                                                    |
| LoRa on the shared bus          | **Plausible lost-flush source on T-Deck only.** SX1262 shares SPI2 with TFT and SD (`variants/t_deck_plus/configuration.h:103`); DIO1 ISR sets a flag, `startReceive`/`readData`/`startTransmit` run from the loop with no bus mutex (`lora_functions.cpp:335, 405, 1569, 1612, 1680`). Heltec V3 and T-Beam have the radio on a dedicated SPI. RAK4631 shares SPI with the W5100S, guarded by `bSPI_ETH_Active` (`lora_functions.cpp:121, 402`). |
| GPS                             | **Not a contributor.** UART only; drained on a 3 s timer in `WZ_GPS_Loop()` (`gps_functions.cpp:862-880`); no display calls in the GPS path; map refresh is user-driven; N25 baud-scan watchdog fixed on this branch.                                                                                                                                                                                                                             |
| Synchronisation in place        | One binary `xSemaphore` (`tdeck_main.cpp:52,117`) taken only by `disp_flush()`, `tdeck_dbg_blink()`, `tdeck_dbg_reflush()`. SD, LoRa, audio, touch never take it. No SPI/I2C arbitration anywhere else in the tree.                                                                                                                                                                                                                               |

#### Items

| ID    | Board(s)                          | Type | Sev.   | Location                                                | Item                                                                                                                                                                                                                                                               | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| ----- | --------------------------------- | ---- | ------ | ------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| TM-01 | T-Deck                            | BUG  | High   | `esp32_audio.cpp:289-477`, `lv_obj_functions.cpp:4045`  | `play_cw()` blocks `loopTask` ~1.1 s per message. Move tone generation off the loop task (queue + audio task) or make `playTone()` non-blocking; harness `audio_stall` scenario must drop from 1.10 s to <100 ms.                                                  | **FIXED `49c482e1`** — audio task + queue; audio_stall 1 552 -> 23 ms                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| TM-02 | T-Deck                            | BUG  | High   | `esp32_audio.cpp:139-144`                               | `play_function` created at priority 50 (clamped to 24, above everything). Set to a sane priority (3); handover C1.                                                                                                                                                 | **FIXED `49c482e1`** — prio 3                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| TM-03 | T-Deck                            | BUG  | High   | `esp32_audio.cpp:102-160`                               | `audioSemaphore` released before playback starts (C2); second message inside one sound frees decoder buffers under the reader. Hold until the task ends.                                                                                                           | **FIXED `49c482e1`** — sequential task, semaphore gone                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| TM-04 | T-Deck                            | PERF | Medium | `esp32_audio.cpp:114,203`                               | `SD.exists()` on SPI2 per message. Resolve tone file once at boot; removes the SD access from the message path (RESUME §4.6).                                                                                                                                      | **FIXED `49c482e1`** — lookup runs in the audio task under the bus mutex                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| TM-05 | T-Deck                            | BUG  | High   | `tdeck_main.cpp:52`, SD/LoRa/audio call sites           | SPI2 has no arbitration: SD (map compose, tone streaming), SX1262 and TFT interleave freely. Either one bus mutex with the audio/SD/LoRa paths taking it, or SPI transactions per user; NOP mitigation stays as belt-and-braces until then.                        | **resolved by analysis** — every SPI2 user except audio runs on loopTask (TFT flush, SD map compose from LVGL callbacks, RadioLib from OnRxDone/loop; DIO1 ISRs only set flags; web/console tasks touch no SPI); the audio task is the only cross-task user and holds the bus mutex since `49c482e1`. Arbitration is complete by construction; what remains is the same-task register state after SD access (TM-07, NOP re-arm).                                                                                                                                                                              |
| TM-06 | T-Deck                            | TEST | High   | `src/test_inject.*`, `command_functions.cpp:4509`       | LoRa harness: `--injectmsg` bypasses the radio. Need (a) raw-frame RX injection through `checkRX`/`decodeAPRS`, (b) `--loratx <n> <ms>` burst, (c) harness scenario correlating `[FLUSH]` CRC with LoRa SPI activity, (d) a second node (`DK5EN-98`) as RF source. | open — lower priority (cumbersome; NOP mitigation stays)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| TM-07 | T-Deck                            | TEST | Medium | `tdeck_main.cpp:469`, `instrument.*`                    | SPI transaction tracing: log which bus user (TFT/SD/LoRa) ran between two flushes and the SPI2 `user/ctrl/clock` registers; identifies the clobbered register (RESUME open item) so the NOP can be replaced by a proper re-arm.                                    | open — lower priority                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| TM-08 | T-Deck                            | PERF | Medium | `lv_obj_functions.cpp` (`update_header_batt_indicator`) | Header labels rewritten every 500 ms unconditionally; only `lv_label_set_text` on change (RESUME §4.3).                                                                                                                                                            | **FIXED** — header labels/colours written only on change; idle invalidations 36.9/s -> 7.0/s (harness idle, 30 s)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| TM-09 | Heltec V3/V4, Stick V3, T-Beam V3 | PERF | High   | `loop_functions.cpp:364-374`                            | OLED on `_1_SW_I2C`: switch to `_F_HW_I2C` on a hardware `Wire` instance (V2/RAK already do), set `setBusClock(400000)`. Expected ~10x faster frame; measure with `[INSTR-FLUSH]` before/after.                                                                    | **FIXED** — Heltec V3/V4, Stick V3: `U8G2_*_F_2ND_HW_I2C` on `Wire1` (SDA 17/SCL 18, `Wire1.setPins()` before `begin()`, 400 kHz, `-D U8X8_HAVE_2ND_HW_I2C=1` in the variant because arduino-esp32 never defines `WIRE_INTERFACES_COUNT`). Measured DK5EN-93: frame push 579 ms -> 34.5 ms, loop max 645 -> 39 ms. T-Beam was already on `Wire` HW I2C (37.8 ms).                                                                                                                                                                                                                                             |
| TM-10 | Heltec V3, T-Beam                 | PERF | Medium | `loop_functions.cpp:1050-1110`, `esp32_main.cpp:3003`   | Full 8-page redraw on every message and every 5 s tick; add a dirty flag and skip the push when the frame is unchanged (compare buffer hash). Partial update (`updateDisplayArea`) only if TM-09 is not enough.                                                    | **FIXED 2026-08-29** — `oledFrameUnchanged()`: in full-buffer mode the frame is drawn into RAM, CRC32 compared with the last pushed frame, identical frames are not sent (`[OLED];skip`, `skipped` in `--oledstat`). Harness `dirty` scenario: 15 s idle pushes nothing on either board; two `--display on` back-to-back -> second reported as skip (Heltec V3 + T-Beam). Note: the head page carries a clock, so frames one second apart differ legitimately; the win is the duplicate push, not the tick |
| TM-11 | all ESP32                         | BUG  | High   | `udp_functions.cpp:529-747`, `esp32_main.cpp:1642-1752` | TD-01 decisive experiment: boot with BLE advertising disabled (or started after `startNetwork()`), 10 power cycles each way. If it flips, fix ordering / coexistence; else test BSSID-pinned `WiFi.begin()` without pin and PMF.                                   | **measured 2026-08-29** on DK5EN-14, 12 boots per arm, same hour: A baseline 4/12 first joins, B BLE advertising deferred until ready 3/12 (BLE hypothesis refuted), C join by SSID only (no channel/BSSID pin) 8/12. New `[WIFI];event;disconnected;reason;N` log: every failure is reason 2 = AUTH_EXPIRE (AP does not finish authentication in time; the driver retry then often succeeds), once 202 AUTH_FAIL. Next: 24-boot confirmation of C, then ship SSID-only join + patient retry instead of the 10-poll give-up. Experiment flags `BENCH_BLE_ADV_LATE`, `BENCH_WIFI_NO_BSSID` (inert by default). |
| TM-12 | RAK4631                           | TEST | Medium | `nrf52_main.cpp`                                        | No loop-period instrumentation on nRF52 (`INSTRUMENT_ENABLED=0`). Port `[INSTR-LOOP]` so the W5100S/SX1262 SPI sharing (`bSPI_ETH_Active`) can be measured, not assumed.                                                                                           | **FIXED** — `INSTRUMENT_ENABLED=1` on nRF52, heap via `mallinfo()`/`dbgHeapTotal()`, `INSTR_LOOPTICK()` in `nrf52loop()`. DK5EN-90 baseline: loop avg 99.7 ms / max 104 ms (the nRF loop is paced ~100 ms), heap 111 832 free.                                                                                                                                                                                                                                                                                                                                                                                |
| TM-13 | all                               | TEST | Medium | `src/instrument.*`, `tools/bench/`                      | Per-subsystem stall attribution: wrap SD, LoRa, audio, display, GPS entry points with an `INSTR_SECTION(name)` and report max per section; makes every later fix measurable on all boards.                                                                         | open                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| TM-14 | T-Deck                            | TEST | Low    | `gps_functions.cpp:862`                                 | GPS control experiment (`--gps on/off`, same injected load, compare `[INSTR-LOOP]` tails) — closes the GPS question with data rather than by reading.                                                                                                              | open                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| TM-15 | T-Deck                            | PERF | Low    | `esp32_main.cpp` boot messages                          | 2 s busy-wait per boot message (8 = 16 s); replace with non-blocking sequencing (RESUME §4.7).                                                                                                                                                                     | **FIXED** — 100 ms LVGL pump per boot message instead of 2 s; CLIENT STARTED 17.8 -> 4.6 s                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |

| TM-16 | T-Deck | PERF | Medium | `esp32_main.cpp` setup, `udp_functions.cpp:529-747` | Boot to `[BOOT];ready` takes 29 s (reset -> network phase settled; `CLIENT STARTED` at ~18 s). Operator sees the start tone, then header icons still blinking. Profile the setup path (SD mount, LVGL build, BLE init, WiFi scan+join) and cut the waits. | partly — boot messages (TM-15), WiFi bring-up (TM-20) and the ready marker fixed (`Serial.printf`, not `printfdeb`: outside `--debug csv` the semicolons were stripped; checks the global `hasIPaddress`). Ready: T-Deck 11 s, Heltec 9 s, T-Beam 9.5 s with WiFi joined. Left: GPS detect ~3 s, TD-01 |
| TM-17 | all ESP32 | BUG | Low | `esp32_main.cpp:3745-3776`, `udp_functions.cpp:737` | `bAllStarted` never becomes true when WiFi joins inside `startNetwork()` (iWlanWait=0 and IP set -> the loop branch that sets it is skipped). Only effect found: the `bHeyFirst && bAllStarted` early HEY/telemetry never fires on a clean boot. Ready marker deliberately does not rely on it. | open |
| TM-18 | T-Deck | PERF | Medium | `tdeck_main.cpp` `mouse_read()` | Trackball "not smooth": GPIO levels are polled every 10 ms (`LV_INDEV_DEF_READ_PERIOD`) and compared to the last level, 10 px per edge. Edges faster than the poll collapse (odd count -> one step, even count -> none). Injected steps are perfect (input scenario: 40/40, p95 30 ms), so the loss is at the edge sampling. Measure with an ISR edge counter vs consumed events under a real roll, then count edges in the ISR and consume them in `mouse_read()`. | **FIXED** — ISR edge counting, consumed in `mouse_read()`; measured on DK5EN-14: old level compare lost ~75 % of edges on fast rolls (186 -> 40, 224 -> 57), edge mode 0 lost, pending 0, repaint p50 8 ms on tab 0 and map. `--balledge off` restores the old path for A/B. |
| TM-20 | all ESP32 | BUG | High | `udp_functions.cpp` `startNetwork()`, `esp32_main.cpp` retry path | **FIXED** — WiFi bring-up froze loopTask ~7 s (delay 1000+200+500, sync `scanNetworks()`) plus `delay(1500)` on the boot retry, and again on every 5-minute retry while unconnected (LoRa RX unserviced on any ESP32 gateway without AP). Now async scan, no delays; measured on DK5EN-14: no gap > 0.7 s in the boot log, loop max 26 ms. Trackball edges banked during a stall are discarded, not replayed. **Operator finding 2026-08-29 (late): with every boot delay removed the station no longer hears the beacons of all nearby APs before it picks one, and can associate with the weakest BSSID of the mesh.** Feels good on the bench, but shipped as is it would degrade every node behind a multi-AP WLAN. | FIXED on the trace branch — **stays OUT of the T-Deck PR** (decision 2026-08-29 late, reverses the evening decision). The AP-selection problem is TM-34's question (b); TM-20 goes upstream only together with a scan/selection strategy that TM-34 has settled. |
| TM-21 | all ESP32 | PERF | Low | `esp32_main.cpp` (`if(bWEBSERVER && iWlanWait == 0) startWebserver();`), `web_functions.cpp:81` | With `--debug on` and no IP (AP unreachable, after the 5-min give-up) every loop pass prints `[WEB]...no ip set` — ~125 lines/s (10 630 in 100 s on DK5EN-93). Pre-existing; print once per state change. | **FIXED** — logged once per state in `startWebserver()`, latch reset when an IP is present |
| TM-22 | T-Beam (all `_HW_I2C` OLED boards) | PERF | Medium | `loop_functions.cpp:379-380` (`#else` branch) | T-Beam is already on `Wire` hardware I2C (37.8 ms/frame, DK5EN-92, SH1106 `_F_`), but the SSD1306 variant still uses `_1_HW_I2C` page mode (8 transfers/frame) and no explicit `setBusClock(400000)`. Same treatment as TM-09; plus TM-10 dirty flag so unchanged frames are not pushed at all. | **FIXED 2026-08-29 (compile-verified only for the SSD1306 half)** — `#else` ladder SSD1306 `_1_HW_I2C` -> `_F_HW_I2C` (page mode is gone from every ESP32 HW-I2C board). DK5EN-92 is the SH1106 (`_F_` already), so the SSD1306 path has no bench node. Bus clock: the T-Beam's Wire was measured already at 400 kHz (35.8 ms/frame ~ 1 KB at 400 kHz), an explicit `setBusClock` changed nothing and was dropped |
| TM-24 | all ESP32 | BUG | High | `udp_functions.cpp` `wifiBeginFromScan()`, `checkWifiPing()` | **No roaming at all** (found 2026-08-29): the SDK is built without 802.11k/v (`CONFIG_WPA_11KV_SUPPORT` not set, no 11r), the firmware never sets `rm_enabled/btm_enabled`, never calls `setAutoReconnect()`, and pins channel+BSSID of the strongest AP seen at scan time in `WiFi.begin()`. Against a mesh WLAN (ORBI63 = router + satellite, two BSSIDs in every scan) that steers clients, the pinned station cannot follow a BSS-transition request and is deauthed/ignored: reason 2 `AUTH_EXPIRE` at join (TM-11 data), and later drops only recovered by the ping watchdog (`checkWifiPing` -> full `startNetwork()` restart). Fix: join by SSID only (driver picks and may switch BSSID), `WiFi.setAutoReconnect(true)`, drop the BSSID pin; optionally enable 11k/v via `esp_wifi_set_config` (`rm_enabled`, `btm_enabled`) once the SDK supports it. Supersedes the TD-01 "which AP" discussion. | open |
| TM-25 | RAK4631 | PERF | Medium | `nrf52_main.cpp` setup | Boot time not profiled on the RAK (no `[BOOT];ready` marker on nRF52 yet; host-timestamped boot log needed). Part of the "boot time on all four platforms" goal. | open — needed for platform parity (next runs) |
| TM-26 | RAK4631 | TEST | Medium | `tools/bench/` | No harness for the RAK: needs a serial scenario set (boot, `--info`, LoRa TX/RX via the other nodes, `--instr` loop/heap, W5100S gateway paths N-20) — the fourth platform of the regression goal. | open — needed for platform parity (next runs) |
| TM-27 | Heltec, T-Beam, E290 | TEST | Medium | `loop_functions.cpp` `sendDisplay1306()` | Pixel-level verification for OLED/e-paper: the U8g2 frame buffer is in RAM — print a CRC32 of the buffer per push (`[OLED];crc`) so the harness can assert screen content, which the T-Deck cannot (no panel readback). | **FIXED 2026-08-29** for U8g2 boards — CRC32 of the frame buffer per push: `[OLED];frame;…;crc;<hex>;skipped;<n>`, `[OLED];skip;…`, `crc`/`skipped` in `--oledstat`. Harness `display` asserts the round trip: every off frame is the blank buffer (`0xefb5af2e` on both boards), every on frame differs. E290 still open (TM-28) |
| TM-28 | E290 Wireless Paper | TEST/PERF | Medium | WP_DISP paths, e-paper driver | Hardware arrives week of 2026-09-01: frame/refresh instrument, harness scenarios (browse loop, message pages, deep sleep), redraw cost per page. | delayed until the hardware is here (week of 2026-09-01) |
| TM-29 | all | TEST | Low | `docs/automation-runner-runbook.md` | Unattended regression runner (bench Mac, schedule, fail on any FAIL/crash marker). Manual procedure and expected outcomes are in the runbook; blockers: TM-26, TM-27, audio assertion, four-node exchange as a script. | open — low priority |
| TM-19 | T-Deck | TEST | Low | `tools/bench/tdeck_harness.py` | Harness gaps: touch injection (none), real-roll trackball capture (TM-18), audible confirmation of tones (operator only). | open |
| TM-23 | T-Deck | PERF | Low | `tdeck_sdmap.cpp`, tile set on SD | Tile format / decoded-tile PSRAM cache (RGB565, QOI, palette PNG; ~95 ms lodepng per tile). **Parked by decision 2026-08-29: stability and functionality first.** Options in `tdeck-findings-20260828.md` §5. | parked |
| TM-30 | T-Deck | BUG | High | unknown — bisect first | **Upstream #1083** (dl9sec, closed _"nicht nachvollziehbar"_): T-Deck Plus touch responds only after tens of seconds once the node has been up ~10 min, while **web interface and OTA stay normal** — a loop-starvation signature that grows with uptime, not a hang. Reporter handed over a free bisect window: `v4.35p.07.26` good, `v4.35p.08.06` and `v4.35p.08.10.2` bad. Upstream landed in that window: the external-radio TCP feature (PR #1072, merged 2026-07-31), `--setlog on/off` (`c78adcc5`, mask corr `a7bf5474`), `HELTEC_V3 ADC_FACTOR`, PL country. Our `external_radio_glue.cpp` is non-blocking by construction, so the obvious suspect does not obviously apply to us — **check, do not assume.** Run: DK5EN-14, 30 min uptime with `[INSTR-LOOP]` on, watch the loop tail and the input latency; then bisect the window if it reproduces. Note none of our fixed items (TM-01…TM-05, TM-08, TM-15, TM-18, TM-20) explain a defect that grows with uptime. | open — analysis |
| TM-31 | all ESP32 (gateway) | BUG | High | `udp_functions.cpp` UDP→LoRa path, `dedup_functions.cpp`, TX queue | **Upstream #568** (HelmutEsterer, closed _"Ich lege das für einen späteren test zur seite"_ — never tested). Evidence recovered and analysed 2026-08-29: the attached gateway log is still live and contains the named packet. DB0SEP-12 → DJ8MEH-46 BBS listing = 7 UDP messages into a LoRa gateway in 45 s, all ~128 bytes; inter-arrival gaps 8, 7, 11, **4**, 8, 7 s. **The one packet the reporter says never reached the target (`A2659021`) is precisely the one arriving on the shortest gap.** Same size as its neighbours, so this is a timing effect, not a length effect — a gateway fed faster than it can radiate at SF11/BW250. Matches our own DG0OPK finding (TX-queue latency, two proven dedup overflows). The log shows only the ingress side, so it does not by itself prove the drop — but it gives an exact bench experiment: feed a gateway N equal-sized UDP messages at decreasing inter-arrival and count LoRa TX vs ingress. Log: `user-attachments/files/21353203`. | open |
| TM-32 | all | BUG | Medium | `esp32_flash.cpp:56-60`, `nrf52_flash.cpp` load path | **Extends the `N-12` fix.** We already validate struct _integrity_ on load (markers + `sizeof` check → `flash_reset()` to defaults). We do **not** validate field _plausibility_: `node_power`, `node_freq`, `node_bw`, `node_sf`, `node_cr` are read straight out of NVS and only the `-20` sentinel triggers a default. A struct that passes the markers but carries an out-of-range radio value goes unchecked into `radio.setOutputPower()`. That is the gap behind upstream **#661** (m-ugo, RAK4631: brown-out corrupts settings, firmware crashes, only recovery is formatting the flash — upstream answer was `--cleanflash`, which needs a working serial console and so does not help a bricked remote node) and **#57** (dl7ata: `RF_POWER: 1` → no LAN, no serial, no BLE; patched narrowly by clamping TX power ≥2 dBm in #140, never by validating the load path). Fix: range-check the radio parameters at load, fall back to the board default and log it. | open |
| TM-33 | T-Deck | TEST | Medium | `tdeck_main.cpp:189`, `command_functions.cpp:873-908`, `lv_obj_functions.cpp:4015-4023` | Regression tests against three upstream T-Deck reports that were closed without a fix. **All three verified still present on this branch 2026-08-29 by reading — none is fixed by our work so far:** (a) **#64** touch fails at boot (`touch: failed`); `touch.begin(Wire)` is a single attempt with no reset pin (`setPins(-1, …)`) and no retry — one failure disables touch for the whole session. Reporter's hypothesis: `TOUCH_INT` GPIO16 is pulled differently on T-Deck vs T-Deck Plus, so GT911 address selection races at reset. Maintainer dismissed it, then conceded he sees it too on a weak battery. (b) **#690** `--display on/off` over serial is inert on T-Deck: the command sets `bDisplayOff`/`bDisplayIsOff` and calls `sendDisplayHead()` — the U8g2 path; `grep bDisplayOff src/t-deck/` returns **nothing**, the TFT backlight is never touched. (c) **#690** `SET > WIFI` shows OFF while WiFi is connected: the switch reflects the stored intent flag `node_wifion`, not the association state — so a node whose WiFi came up because gateway/webserver/netconsole forced it (cf. #1059) shows a wrong switch. Plus **#267**: T-Deck battery value reported frozen on the dashboard while a T-Beam beside it was fine — bench check on DK5EN-14 against the beacon, not the local header. | open |
| TM-34 | all ESP32 | RESEARCH | High | `udp_functions.cpp` `startNetwork()`, `wifiBeginFromScan()`, `doWiFiConnect()`, `checkWifiPing()`, `wifiEventLog()`; arduino-esp32 / ESP-IDF WiFi driver and `sdkconfig` of the framework build | **WiFi investigation — a separate research track, not part of the T-Deck PR (opened 2026-08-29 late).** Trigger: TM-20 removed every boot delay and the node now associates before it has heard all beacons — sometimes with the weakest AP (see TM-20). TM-24 (no roaming, BSSID pinned) and TD-01/TM-11 (`AUTH_EXPIRE` on first join) are the same problem seen from other sides. Questions to answer, each with evidence from the driver source/sdkconfig and a bench measurement, before any of it is coded: (a) **Driver capabilities** — which arduino-esp32 / ESP-IDF version is in the build, what the WiFi stack supports and what is compiled out (`CONFIG_WPA_11KV_SUPPORT`, 11r, `rm_enabled`/`btm_enabled`, `WIFI_FAST_SCAN` vs `WIFI_ALL_CHANNEL_SCAN`, `sort_method`, `threshold.rssi`, `scan_method`, PMF, band selection on the C3/S3 targets). (b) **Scan and selection** — options for scanning long enough to hear every AP of the SSID (passive vs active scan, per-channel dwell `scan_time`, full-channel scan) and then joining the best one without re-introducing a loop stall (async scan is already in place; what is missing is the wait/selection policy, not the blocking). (c) **Stall log** — the WiFi stack can stall the whole node, blocking even LoRa reception: add log output that names the stall (which call, how long, from which task — `[WIFI];stall;…`, loop-gap instrument correlated with `wifiEventLog()` events) so the field can report it and we can reproduce it. (d) **SSID-only association** — can we drop the BSSID+channel pin in `WiFi.begin()` and let the driver choose (the `BENCH_WIFI_NO_BSSID` experiment: 8/12 vs 4/12 first joins on DK5EN-14); what changes in AP selection when we do. (e) **Re-connecting** — `WiFi.setAutoReconnect()`, driver-side reconnect vs our `checkWifiPing()` -> full `startNetwork()` restart; who owns reconnect, and how the two must not fight. (f) **Roaming** — what the station can do without 11k/v/r (RSSI-threshold rescan, `esp_wifi_scan` while connected) and what needs the SDK options; behaviour against a steering mesh (ORBI63 router + satellite on the bench). (g) **Band and AP steering** — how the node reacts to BSS-transition requests and to APs that deauth/ignore a client to push it elsewhere; 2.4 GHz-only hardware against dual-band steering; whether pinning the mesh's 2.4 GHz BSSID is ever the right call. Deliverable: a findings doc (`docs/wifi-findings-<date>.md`) with one recommendation per question, then a fix plan with its own bench protocol (24-boot runs per arm, `scratchpad/bootloop.py` pattern) and its own PR — separate from the T-Deck PR. Absorbs TM-24 and the TD-01/TM-11 confirmation run. | open — research, next after TD-03; owns TM-20's upstream fate, TM-24, TD-01/TM-11 |

#### Bench fleet (scanned 2026-08-29, all four live on USB)

| Port                            | USB bridge (VID:PID, serial)                            | Board                                                     | Call     | Env                      | Open-port behaviour                                                               |
| ------------------------------- | ------------------------------------------------------- | --------------------------------------------------------- | -------- | ------------------------ | --------------------------------------------------------------------------------- |
| `/dev/cu.usbmodem1101`          | Espressif USB-JTAG `303a:1001`, MAC `e0:72:a1:ad:65:e0` | T-Deck Plus, GPS RX44/TX43, SD, TFT                       | DK5EN-14 | `t_deck_plus`            | Reboots on every open; wait for `CLIENT STARTED` (+~11 s)                         |
| `/dev/cu.usbserial-0001`        | CP2102 `10c4:ea60`, serial `0001`                       | Heltec V3, SSD1306 OLED, ext. GPS RX47/TX48               | DK5EN-93 | `heltec_wifi_lora_32_V3` | Rebooted on open even with dtr/rts low; `-b 460800` safest                        |
| `/dev/cu.usbserial-573C0005841` | CH9102 `1a86:55d4`, serial `573C000584`                 | T-Beam v1.2 (AXP2101), SX1276, SH1106 OLED, GPS RX34/TX12 | DK5EN-92 | `ttgo_tbeam`             | Rebooted on open; 921600 flash baud fails, use 460800                             |
| `/dev/cu.usbmodem201301`        | RAKwireless `239a:8029`, serial `230D6EBB3266D20E`      | RAK4631 (nRF52840), no display, W5100S gateway            | DK5EN-90 | `wiscore_rak4631`        | Does **not** reset on open; needs `dtr=True` or it stays silent; `--info` answers |

All four run 4.35p (RAK build Aug 22 2026, `Flash-Version 20260724`). The three ESP32 boards join
`ORBI63`; the T-Deck saw it at -80 dBm, Heltec -53, T-Beam -56 at the same desk (TD-01 hardware
note holds). Query helper: `tools/bench/serial_session.py PORT [--wait-boot] --info`.

**Cross-board regression 2026-08-29 (after TM-01..04, TM-15, TM-18, TM-20, UP-01, upstream merge):**
Heltec V3 (`DK5EN-93`), T-Beam v1.2 (`DK5EN-92`), RAK4631 (`DK5EN-90`) flashed from
`tdeck-partial-refresh-trace`; 100 s boot observation each: no reset loops, no crash markers,
LoRa init OK, `--info` answers, BLE up, WiFi path unchanged in behaviour (TD-01 first-join failure
on all, then the 5-min wait). Over-the-air: `--sendpos` from T-Beam, Heltec and RAK received by
every other node (LoRa debug on, `MH-LoRa` lines), T-Deck TX evidenced by `DK5EN-14` in the
Heltec and RAK `--mheard` lists. Only finding: TM-21 (`[WEB]` debug spam, pre-existing).

**OLED harness 2026-08-29** (`tools/bench/oled_harness.py --list`, Heltec V3 / T-Beam / every U8g2
board; `--port` selects the node): boot (Wire1 ack, ready), **pos** (`--injectpos` -> position
page; runs first because a shown text blocks positions for the ping time, `offwait_ms` in
`--oledstat`), inject (`--injectmsg` -> message page, ring advances), pages (`--btn click`
through the ring), display (`--display off/on` x3, both redraw), track (`--btn triple`), timing
(`[INSTR-FLUSH]` = OLED frame push, loop max). Firmware hooks (fork-only): `--btn
click|double|triple`, `--oledstat`, `--oledlog on/off` (`[OLED];frame;us;..;page;..`),
`--injectpos` on non-T-Deck boards via `inject_position()`. A node in persisted track mode is
switched to `--track off` for the run and restored. 7/7 on DK5EN-93 and DK5EN-92.

**Pitfalls learned:** `printfdeb()` strips `;` unless `--debug csv` — bench markers must use
`Serial.printf`; U8g2's `_2ND_HW_I2C` is a silent no-op without `U8X8_HAVE_2ND_HW_I2C` (frame
"push" then takes 4 ms and the panel stays dark); `sendDisplayPosition()` is gated by
`bPosDisplay`, `DisplayOffWait`, `pageHold` and `bDisplayTrack`.

**Harness state 2026-08-29** (`tools/bench/tdeck_harness.py --list`): boot, idle, tabs, drawer,
inject, audio, audio_stall, sleep, screen, **map** (10 stations 0.3-400 km, full zoom sweeps,
`center_err 0/0`, crash watch), **nav** (drawer -> tab -> drawer over all tabs, settings page
scrolled to the bottom and back), **input** (keys via `--key`, trackball via `--ball`, event ->
repaint latency), heap. `--scenario a,b --skip c`, `--list`. Every run waits for `[BOOT];ready`
and switches the panel on (`[TFT];on/off` lines show the 30 s backlight timeout). All 13 pass on
DK5EN-14 at `tdeck-partial-refresh-trace` HEAD after the audio wave.

**Harness state 2026-08-29** (`tools/bench/tdeck_harness.py --list`): boot, idle, tabs, drawer,
inject, audio, audio_stall, sleep, screen, **map** (10 stations 0.3-400 km, full zoom sweeps,
`center_err 0/0`, crash watch), **nav** (drawer -> tab -> drawer over all tabs, settings page
scrolled to the bottom and back), **input** (keys via `--key`, trackball via `--ball`, event ->
repaint latency), heap. `--scenario a,b --skip c`, `--list`. Every run waits for `[BOOT];ready`
and switches the panel on (`[TFT];on/off` lines show the 30 s backlight timeout). All 13 pass on
DK5EN-14 at `tdeck-partial-refresh-trace` HEAD after the audio wave.

#### Order of work (proposal)

1. **Instruments first** (TM-13, TM-07, TM-12, TM-06) — every fix below must show up as a number.
2. **T-Deck audio** (TM-01, TM-02, TM-03, TM-04) — the visible freeze; independent of the bus.
3. **T-Deck bus** (TM-05, then retire the NOP once TM-07 names the register).
4. **Heltec/T-Beam OLED** (TM-09, then TM-10 only if needed) — one-line constructor change, largest win per line.
5. **WiFi** (TM-11) — one experiment decides the direction. _Superseded 2026-08-29 late: the WiFi work is its own research track, TM-34 (questions a-g); nothing WiFi-related goes into the T-Deck PR._
6. TM-08, TM-14, TM-15 as filler.

### 3.8g Upstream sync 2026-08-29 — state of `dev`, incoming review, branch model

Fetched 2026-08-29: `upstream/dev` = `2cb6bb4d` (14 commits past our base `fc83554e`, PRs
#1104-#1112). Net delta is small — **5 files, +18/-43** — most of the churn is same-day add/remove.

**Our PRs.** #1102 (`0cac4aea`, stability, 64 files) is merged and intact: no upstream commit has
touched any of its files since. #1103 (`FWDATE` buffer) is dead: `322f1514` removed the `FWDATE`
key altogether instead of fixing the frame budget (`issue-ble-i-register-mtu-20260828.md` still
applies to the root cause). DL9SAU's #1090/#1091/#1093 were reverted on 08-28 (#1105-#1107);
#1092 survives.

**Incoming review — verified 2026-08-29** (`/fable-review`, six finders, every claim checked
against the tree; full verdict in
[`review/2026-08-29-upstream-sync-verdict.md`](review/2026-08-29-upstream-sync-verdict.md),
catalogue entries `08-defect-catalogue.md` §2b):

| ID    | File                                   | Verified finding                                                                                                                                         | Sev.   | Action                                                                             |
| ----- | -------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------------------------------------------------------- |
| UP-01 | `mheard_functions.cpp` (2 sites)       | `serializeJson` bound is the JSON's length, not `sizeof(bleBuffer)-1`. Worst case today 284/299 B (15 B headroom) — latent, goes live with the next key. | Medium | **FIXED `b586daee`** — `bleJsonFrame()` + `test_ble_json_frame`; goes into the PR. |
| UP-02 | `lv_obj_functions.cpp` `add_map_point()` | `delay(40)` x2 in `add_map_point()`, reached from `OnRxDone` per position beacon (80 ms) and 30x from `refresh_map()` in one click callback (2.4 s). On our tree the G01 hunk had already removed the pair; one `delay(10)` in the slot-recycling branch (after 30 on-screen stations) remained. | High | **FIXED 2026-08-29** — last delay dropped; `add_map_point()` is delay-free. Harness `map --map-stations 40` exercises the recycle branch (wrap to `map_points` 11, 40/40 injected, no crash); loop max in the injection window 124 ms before / 153 ms after = noise, the win is structural (no blocking on the RX path). Harness fix on the way: station calls are `DK5EM-nn` — `DK5EN-14` collided with the bench node's own call and each collision recomposed the map (728 ms loop stall, an artefact). |
| UP-03 | `tdeck_sdmap.cpp:304-305`              | x scaled by 320-32, y by 256, image zoomed 1.25x: markers wrong in y at every zoom. Magic numbers.                                                       | High   | Ours (`db298c49`) wins the merge; two hunks merge silently — check by hand.        |
| UP-04 | `lv_obj_functions.cpp:1758,1766`       | G01 NULLing — Kurt fixed it independently, same as ours.                                                                                                 | —      | Keep his hunk; drop ours from the PR.                                              |
| UP-05 | `command_functions.cpp:4961`           | `I` register at 239/244 chars with six group calls.                                                                                                      | Low    | Watch.                                                                             |
| UP-06 | `regex_functions.cpp:9` (pre-existing) | Callsign regex `[0-9]+` unbounded — 119-char callsigns pass validation.                                                                                  | Medium | Trace consumers, small PR + test.                                                  |

**Process rule from here on:** every `git merge upstream/dev` into fork main is preceded by a
review of the _net_ diff since the last merge base (`git diff <base> upstream/dev`), findings
filed as `UP-nn` here and in `08-defect-catalogue.md`. Upstream has no CI and no tests; our
native suites are the only gate MeshCom code passes through, so upstream changes in covered
areas get a test added at merge time.

**Plan agreed 2026-08-29:** (1) docs, commit, push; (2) tag and delete stale branches (§4);
(3) `/fable-review` on `fc83554e..upstream/dev`; (4) **done 2026-08-29** — `upstream/dev` merged into
`tdeck-partial-refresh-trace` (resolution in the merge commit message; native 45/45, four targets build);
(5) bench-verify on DK5EN-14; (6) T-Deck PR built from `upstream/dev` per §4, UP-01 fix included.

## 3.9 Hardware-Handover nRF52 (RAK4631) — Stand 2026-08-19 00:58

Angeschlossen ist jetzt ein **RAK4631** statt des Heltec V3. Der Heltec haengt nicht mehr
am USB (`/dev/cu.usbserial-0001` weg). Hier steht alles, was zum Weitermachen noetig ist.

### Board-Identitaet

|                 |                                                                                                   |
| --------------- | ------------------------------------------------------------------------------------------------- |
| Port            | `/dev/cu.usbmodem2101`                                                                            |
| USB             | `WisCore RAK4631 Board`, VID `0x239A`, PID **`0x8029` = Applikation** (Bootloader waere `0x0029`) |
| Rufzeichen      | **DK5EN-90** (vom Benutzer gesetzt, hat das Flashen ueberlebt)                                    |
| Node-ID         | `48A4690D` → BLE-Name **`MC-690d-DK5EN-90`**                                                      |
| Firmware jetzt  | unser Branch, `build: Aug 19 2026 / 00:47:55`, `Flash-Version 20260724`                           |
| Firmware vorher | `build: Mar 24 2026`, `Flash-Version 20260324` (vor der ganzen Kampagne)                          |
| BTCODE          | 100000                                                                                            |
| Peripherie      | **nur LoRa.** Kein Ethernet-Kabel, kein WLAN. Hoert DK5EN-98 ueber Funk                           |

### Wie das Board geflasht wurde (der Weg, der funktioniert hat)

```bash
pio run -e wiscore_rak4631
pio run -e wiscore_rak4631 --target upload --upload-port /dev/cu.usbmodem2101
# -> "Device programmed."
```

`adafruit-nrfutil` ueber serielles DFU. **Kein Doppeldruck auf Reset noetig.** Der Uploader
macht den 1200-Baud-Touch selbst.

### DFU-Modus — was NICHT funktioniert hat, und warum

- **Doppeldruck auf Reset:** das Board rebootet (Port re-enumeriert), meldet sich aber immer
  wieder als Applikation (`0x8029`). Es erscheint **kein** `/Volumes/RAK4631`. Mehrfach
  probiert, auch mit laufendem Watcher auf `/Volumes`.
- **1200-Baud-Touch von Hand:** loest ebenfalls nur einen Reset aus, kein Laufwerk. Grund:
  der Adafruit-Core ruft dabei `enterSerialDfu()` auf, nicht `enterUf2Dfu()` — das ist der
  **Serial-only-DFU-Modus ohne Massenspeicher**. Genau den braucht `adafruit-nrfutil` aber,
  deshalb klappt der Upload-Weg oben.
- Ergebnis: **der UF2-Laufwerk-Weg ist an diesem Board bisher nicht erreichbar.** Die
  UF2-Datei liegt trotzdem gebaut unter
  `.pio/build/wiscore_rak4631/firmware.uf2` (Rezept in `CLAUDE.md`), falls das Laufwerk
  doch mal auftaucht.

### Fallstricke, die Zeit gekostet haben

1. **PlatformIO meldet `SUCCESS`, auch wenn das DFU-Upload fehlschlaegt.** Der erste Versuch
   brach mit `PortNotOpenError` ab — und darunter stand trotzdem `1 succeeded`. Nur
   `Device programmed.` im Log beweist, dass wirklich geschrieben wurde.
2. **Die alte Firmware hatte einen haengenden CDC-RX-Pfad:** Lesen ging, aber _jedes_
   Schreiben auf den Port liess ihn danach minutenlang verstummen. Deshalb schlug der erste
   Upload fehl und `--setcall` kam nie beim Parser an. **Mit unserer neuen Firmware ist das
   weg** — `--info` antwortet zuverlaessig. Ob das an unserem printfdeb-Fix (`bd10b636`,
   `Serial.write` statt `Print::printf` mit `malloc` pro Zeile) liegt, ist plausibel, aber
   nicht bewiesen.
3. **`flash_reset()` verliert die Einstellungen NICHT** — entgegen der ersten Lesart. Die
   Reihenfolge in `nrf52_main.cpp:508-533` ist: `init_flash()` laedt die Settings in den
   RAM, `flash_reset()` schreibt nur die _Datei_ mit Defaults neu, danach schreibt
   `save_settings()` den weiterhin gefuellten RAM-Stand zurueck. Rufzeichen, BTCODE, ATXT,
   NAME, CTRY haben den Versionswechsel 20260324 → 20260724 unveraendert ueberstanden.
4. **Kein Ethernet ist der Normalzustand** dieses Boards. `Failed to configure Ethernet
using FIX/DHCP` + `GATEWAY 4.0 RUNNING ETH no connect` beim Boot sind **kein** Defekt;
   der Boot dauert dadurch ~18 s laenger. (Ueberholt am 2026-08-21 nachmittags: die Init
   **kann** doch haengen und blockiert die Loop periodisch — siehe `N-20`.)

### Neu gebaut, aber auf Hardware noch ungetestet

`--dfu` (Commit `7bac915a`, nur nRF52): startet ueber `enterUf2Dfu()` in den
UF2-Bootloader, erreichbar per BLE/Seriell/Netz-Konsole. Der Sprung ist um 2 s verzoegert
(Flag `bEnterDfu` ueber den `rebootAuto`-Pfad), damit die Quittung noch rausgeht. Damit
sollte sich das Laufwerk kuenftig auch ohne physischen Zugriff erzeugen lassen — **das ist
der erste Test fuer morgen**, denn ueber den Doppeldruck kam dieses Board nie in den
UF2-Modus. _(Nachtrag 2026-08-21: der Test schlug fehl (`N-19`), der Befehl wurde am
selben Tag repariert und zweimal end-to-end verifiziert — siehe den Abschnitt zum
zweiten Durchgang.)_

### Offener Punkt: BLE vom Pi aus

`mcapp.local` ist sauber von DK5EN-98 getrennt (`{"success":true,"message":"Disconnected"}`),
findet **DK5EN-90 aber nicht** im Scan — nur DK5EN-98 taucht auf (und das mit `rssi: 0`).
Der Benutzer kommt per BLE an das Board heran, der Pi offenbar nicht: vermutlich schlicht
ausser Reichweite, weil der RAK woanders liegt. Morgen zuerst klaeren, bevor BLE-Tests am
RAK geplant werden.

### Was am nRF52 noch zu tun ist

**Stand 2026-08-21** — sechs der sieben Wave-2-Nebenlaeufigkeitsbefunde sind auf
`wiscore_rak4631` behoben und gebaut (Details im nächsten Abschnitt). Noch offen:

- `N-14` — TX-Ring mit mehreren Schreibern ohne Mutex. **Bewusst nicht in dieser Session
  gefixt** — Re-Verifikation zeigte einen groesseren Scope als katalogisiert (elf
  Aufrufer schreiben den Ring-Slot selbst, nicht `addTxRingEntry()`; ein sauberer Fix
  braucht eine "Slot reservieren, dann schreiben"-Umkehr an allen elf Stellen). Details:
  STATUS-Box zu `N-14` in `08-defect-catalogue.md`.
- `DRY-21` — `nrf_eth.cpp` dupliziert `udp_functions.cpp`, inklusive abweichendem ACK-Code
  (`0x01` statt `0x02`). **Ohne Ethernet-Kabel nicht am Geraet testbar**
- Wave 0.2-Rest — nur `wiscore_rak4631` erledigt; `heltec_t114`/`t_echo` offen (siehe `CFG-01`)
- `N-12` — `FLASH_VERSION`-Migration, weiter aufgeschoben
- `CFG-01` — root cause (Sektionskollision) weiter unten, nur die drei bekannten Symptome
  sind behoben

~~`CONC-15`~~/~~`CONC-16`~~/~~`CONC-18`~~ (Ring-Indizes), ~~`CONC-17`~~ (zerrissene
Settings-Kopie), ~~`N-15`~~ (bereits durch `CONC-14` geschlossen, re-verifiziert),
~~`N-16`~~ (`Radio.Send()` in `taskENTER_CRITICAL()`) und ~~`DRY-22`~~
(`checkSerialCommand()`-Drift) sind erledigt — siehe naechster Abschnitt.

### 2026-08-22 (dreizehnter Durchgang) — QA-Welle 2: Testsuite-Audit (fable-review) umgesetzt

/fable-review ueber die gesamte Testsuite: 6 Finder-Winkel, adversariale
Verifikation, Verdict `docs/review/2026-08-22-testsuite-verdict.md` (8
bestaetigte Findings, 6 widerlegte Claims mit Beweis), zwei Fix-Wellen,
unabhaengiger Fable-Advisor APPROVED (Literale handgerechnet, Gate frisch
gefahren). Kernpunkte (`30ef55d7`):

- Golden-Fence gehaertet: Regenerations-Laeufe schlagen IMMER fehl;
  canonical() um appoff/srccall/srclast/pathcnt erweitert (f004/f005
  zeigen jetzt das vorher unsichtbare app_offline-Bit); Roundtrip prueft
  byte5-Flags (Mesh maskiert wegen bMESH-Encoder-Semantik).
- txring: defensive Laengen-Invariante (len 0/>255 -> -1) + Randtests.
- Shims: BOARD_HARDWARE-ODR-Fix in 4 TUs, millis-Uhr uint32_t (Wrap
  physisch), substring-Swap-Semantik, millis-Leck geschlossen.
- Mock ent-zirkularisiert: CONF/DATA gegen handgerechnete Byte-Literale,
  Expiry-Test, Suite 6s->2.8s, README-Claims korrigiert.
- Dokumentiert ohne Aenderung: test_compress inert (upstream),
  external_radio-Spiegel-Suiten (upstream), Mock-Routing-Annahme.

Suite-Staende: native_aprs 31/31, native 15/15, Mock 14/14, CI traegt
beide native Envs + Ressourcen-Delta.

### 2026-08-22 (zwoelfter Durchgang) — QA-Welle 1: Orakel in CI, TX-Ring nativ getestet (fand sofort N-24), Mock-Server, Ressourcen-Waechter

Orchestrierte Welle auf Benutzerauftrag ("i really don't trust the code, we
must have automated testing"), alle Streams committed + gepusht:

- **CI (`7609f589`):** native_aprs (das komplette Orakel) laeuft jetzt im
  Unit-Test-Job — lief vorher nur lokal.
- **TX-Ring nativ testbar (`6eb6929e`):** Verbatim-Extraktion des Ring-Kerns
  nach `src/txring_functions.cpp` (freigegeben), `test/test_txring/` mit 11
  Tests (Prio-Klassifizierung komplett, Overflow/Eviction/Drop, Wrap,
  N-14-Invarianten, 200-Schritte-Stress mit Torn-Write-Pruefung).
  **Die Suite fand am ersten Tag einen echten High-Defekt: N-24** —
  indirekte Eviction verwaiste einen belegten Slot (nie gesendete, ggf.
  CRITICAL-Nachricht unsichtbar + unbilanziert). **Gefixt `cf74e08e`**
  (Umzug des iRead-Eintrags in den freigeraeumten Slot, unter dem
  N-14-Lock), Nebenfund clearSlotFirst-Luecke gefixt `a0bcd9da`. Bench-
  Smoke nach Extraktion UND nach Fix (DM + Heltec-ACK ueber Funk+Server).
- **Mock-MeshCom-Server (`a55bd404`):** `tools/mock/` — Server-Testdouble
  fuer Port 1990 (KEEP-Registry, byte-genaues BEAT, DATA-Validierung mit
  GATE-Redistribution, CONF-TLV-Builder), 12 stdlib-unittest-Faelle mit
  echten Korpus-Frames. Dabei doc-11-Praezisierung: MAX_ZEROS prueft nur
  den ABSCHLIESSENDEN 2-Byte-alignten Null-Lauf (in §2.2 eingearbeitet).
- **Ressourcen-Waechter (`4d8cb787`):** `tools/resource_watch.py` +
  `tools/resource_baseline.json` (4 Envs geseedet) + CI-Schritt
  (Report-only, ::warning bei Wachstum/Headroom-Schwelle). Ersetzt
  ram_snapshot.py (C-12).

Offen in dieser Welle: /fable-review ueber die Testsuite + Umsetzung der
Findings (Welle 2, laeuft an).

### 2026-08-22 (elfter Durchgang) — N-20-Fault-Injection-Soak BESTANDEN, Mock-Peer als Testrig

Kabel-Flap-Soak nach dem Backlog-Rezept, Benutzer zog/steckte das Kabel:
Harness auf dem Mac (drei Kanaele: EXTUDP-Listener :1799, DM-Injektor alle
10 s mit echtem Heltec-ACK-Rueckverkehr, Serial-Echo-Probe) plus
compile-gateter 500-ms-Sequenz-Heartbeat aus dem Loop (`MC_TEST_HOOKS` in
`getExternUDP()`, als Test-Infrastruktur eingecheckt). Ergebnis ueber 5
Flaps (4–16 s, mitten im Verkehr): Loop-Task blockierte keinen einzigen
Takt (Heartbeat lief in jeder Unplug-Phase im exakten 500-ms-Raster
weiter), 0 Serial-Stalls, 0 seq-Luecken, kein Reboot, Auto-Reconnect.
N-20-Testverpflichtung damit erfuellt (zusammen mit dem 5-min-Soak vom
21.08.); W5100S-Bibliotheks-Warteschleifen bleiben dokumentiertes
Rest-Thema fuer Upstream. Nebenbei: Mac-DHCP-Lease war gewandert — EXT IP
von .64 auf .58 umgestellt und gespeichert. RAK laeuft wieder mit
Produktionsbuild (Hook nicht einkompiliert, per ELF-strings verifiziert).

### 2026-08-22 (zehnter Durchgang) — Fix-Welle CFG-01 + N-12 + N-14; Neufund N-23 (Brick-Falle) gefixt

Orchestrierte Welle (3 Scouts, 3 Implementer, Gate + Bench durch den
Orchestrator). Vier Commits, alle Hardware-verifiziert am RAK4631
(+ Heltec V3 fuer den ESP32-Pfad):

- **CFG-01 (`24122ed4`):** kollidierende `[nrf52]`-Sections → eine explizite
  `[nrf52_base]` in der Root-platformio.ini. Beweis: `pio project metadata`
  vor/nach bitgleich fuer alle drei Envs. Design-Erkenntnis: der Leak ist
  verhaltenstragend (BOARD_RAK4630 schaltet 60+ Stellen; t_echo kompiliert
  wiscores variant.cpp) — deshalb explizit gemacht statt entfernt.
  Nebenbefund: t114/t_echo bauen laengst mit -Werror (geerbter
  build_src_flags) — Wave-0.2-Rest faktisch erledigt.
- **N-12 Teil-Fix (`14e826b8`):** flash_reset() invalidiert init_flash_done
  (--cleanflash liefert echte Defaults, am Geraet bewiesen: XX0XXX-00);
  Groessen-Check der Settings-Datei mit In-Place-Recovery statt
  Format+Reboot. Struct-Vereinheitlichung bleibt Upstream-Epic.
- **N-23 NEU + FIXED (`b62976c9`):** bei der Bench-Verifikation gefunden:
  `--extudp on` ohne Gateway/Webserver brickt den Node dauerhaft
  (startExternUDP auf uninitialisiertem W5100S im sofort feuernden
  15-Minuten-Block; gespeicherte Config → Falle ueberlebt Reboots und
  Neuflashes; Rettung nur per 1200-Baud-Touch). Fix: Start nur bei
  neth.hasIPaddress. Erster Teil des N-20-Backlogs.
- **N-14 (`efb2381b`):** TX-Ring-Enqueue komplett in addTxRingEntry() unter
  taskENTER_CRITICAL (nRF52), 16 Aufrufstellen umgestellt, Rueckgabe
  Slot/-1. Bench: Loop-Enqueue (DM 91→90, Prio CRITICAL korrekt),
  Timer-Task-ACK (:ack091 mit msg_id-Match zurueck an 91), Timer-Task-Relay
  (90 relayt 91er-HEY mit Signal-Report). Wichtig: der Boot-Haenger waehrend
  der Verifikation war NICHT N-14 (Bisect mit gestashten N-14-Dateien →
  identischer Haenger) sondern N-23.

Bench-Endzustand: RAK DK5EN-90 als Gateway am OE-Server (IP .68, KEEP/NTP),
EXTUDP → 192.168.68.64, Webserver on, Gruppen restauriert; Heltec DK5EN-91
mit neuem Build. Gate: wiscore (-Werror), heltec V3, t114, t_echo gruen;
native 15/15, native_aprs 17/17.

### 2026-08-21 (neunter Durchgang) — §2-Quervalidierung gegen mc-chat-Softnodes und Upstream-Reflector-Spec

Auf Benutzerauftrag: doc 11 §2 (Server-UDP) gegen die zweite unabhaengige
Implementierung validiert — `mc-chat/meshcom_mock/` (Softnodes live am OE-
und DL-Server; `protocol.py`, `decoder.py`, `node.py`) plus die dort
referenzierte Upstream-Spec `icssw-org/MeshCom-Reflector`
(`protocolls/refelctor_connections.md`, Fehlerkatalog in
`mc-chat/doc/proto-deviations.md`). Jede uebernommene Aussage gegen den
Firmware-Code verifiziert. Neu in doc 11:

- **§1:** msg_id-Komposition `((gw_id&0x3FFFFF)<<10)|(counter&0x3FF)`, Wrap
  bei **1000** (Clamp auf 999 an drei Stellen, `loop_functions.cpp:3131ff`;
  Grund: 3-stelliges `{NNN`-Suffix); Ack-Text exakt `%-9.9s:ack%03i`
  (`:4198`, Space = Padding); `{NNN` ohne schliessende Klammer, nur
  `{pong}{NNN}` mit (`:3454` vs `:3208`); neues **§1.7** Steuer-Payloads
  {CET} (Zeit nur ohne RTC/GPS/NTP), {SET}N;M; (setzt max_hop_text/pos!),
  {MCP} (Fernwirken mit Passwort+Lfd-Check), {ping}/{pong} — alle ohne
  Retransmission (`:3527`).
- **§2:** Upstream-Spec referenziert inkl. dreier dokumentierter
  Spec-Fehler (DATA-Byte 34/35 sind ASCII-Modulationsziffern "03", kein
  Laengenbyte); BEAT-Binnenstruktur `BEAT+0x00+len+call[+0x01+len+status]`
  (Firmware ignoriert sie, Mock-Server muss sie senden; Server antwortet
  auf jedes KEEP); **CONF ist nRF52-only** — ESP32-`getUDP()` hat nur
  GATE/BEAT-Zweige (`udp_functions.cpp:148ff`), der Kommentar nennt CONF,
  Code fehlt: Plattform-Divergenz im DRY-21-Klon.
- **Neues §5:** INTERLINK-Abgrenzung — icssw Server-zu-Server-Feed, UDP
  1985, `DNCLOUD`/`HBMASTER`/`DNCDATA`+JSON/`DNCBYE`; Firmware spricht es
  nicht; mcmap (`/Users/martinwerner/WebDev/mcmap`, `proxy/src/interlink/`)
  konsumiert den Mesh ausschliesslich darueber (+HTTP-Scraper), mc-chat
  `meshcom_mock/interlink.py` ist die Ursprungs-Implementierung.
- Referenzvektoren-Kapitel nach §6, test_aprs_spec dort ergaenzt.

Nebenbefund fuer den Benutzer (mc-chat-Repo, nicht von uns geaendert):
`mc-chat/doc/MeshCom-IoT-Mock.md` §13 Punkt 10 behauptet Counter-Wrap bei
1024 ("modulo-1000 was incorrect") — das widerspricht sowohl der Firmware
als auch mc-chats eigenem, neuerem `protocol.py` (Wrap 1000, ausfuehrlich
begruendet). Die Doku-Zeile ist veraltet.

### 2026-08-21 (achter Durchgang) — Orakel-Rest geschlossen: Spec-Vektoren + BLE-Vertiefung; Branch gepusht

Auf Benutzerwunsch zuerst gepusht: `v4.35p_prio` → `origin` (DK5EN-Fork,
`28798e48..656dd563`), danach die beiden letzten Orakel-Offenpunkte:

- **Spec-abgeleitete Vektoren (`test/test_aprs_spec/`):** Frames Byte fuer
  Byte aus doc 11 konstruiert — eigener Builder, eigene FCS-Summe, bewusst
  unabhaengig von `encodeAPRS()`, damit Dokument und Code gegeneinander
  antreten. Abgedeckt: alle 16 Byte-5-Flag-Kombinationen + Hop-Nibble,
  FCS-Regel, Gruppen-/Sonderziele (9999/100001/`*`/`H`), Trailer-Optionalitaet
  beim Decoder, FW-Sub-Platzhalter 0x7E/0x00→`#`, 0x41-Klassifikation VOR der
  Mindestlaengen-Pruefung, elf Verwurfregeln (FCS, Regex, "DE"-Verbot,
  fehlende Terminatoren, FW 1..34), Encoder byte-genau gegen den Builder.
  `pio test -e native_aprs` jetzt 13/13 — **keine Abweichung Dokument↔Code.**
- **BLE-Kapitel vertieft (doc 11 §4):** Hello-Handshake komplett
  (`04 10 20 30` offen bzw. 36-Byte-Frame mit SHA-256 ueber die
  6-stellige PIN, `phone_commands.cpp:307ff`; falscher Hash → Disconnect;
  Querabgleich `MCProxy/config_loader.py:120`), Post-Hello-Config-Burst
  (`config_cmds[]` → 0x44-JSONs → MHeard → `CONFFIN` nach 3-s-Settle, einmal
  pro Hello) und alle 15 `0x44`-Schemata als Tabelle (I, SE, S1, SW, S2, SN,
  W, G, SA, IO, TM, AN, MH, CONFFIN). **Korrektur:** MHeard geht als
  `MH`-JSON zum Phone; der `0x91`-Binaer-Zweig hat keinen Produzenten mehr
  (Legacy). Quirk dokumentiert: jede Notification traegt `blelen+2` Bytes.
- **Umnummerierung:** das Wire-Format-Dokument kollidierte mit
  `09-concurrency-map.md` und heisst jetzt `11-wire-format.md`; Verweise in
  Katalog, resume, Tests und Architektur-README nachgezogen; die
  "known gaps"-Zeile (fehlende Wire-Format-Spec) geschlossen.

Damit ist der Orakel-Plan aus doc 08 §4 vollstaendig umgesetzt: Capture-Hook,
native Umgebung, Interop-Vektoren, Differential-Runner + Korpus,
Wire-Format-Dokument, Spec-Vektoren.

### 2026-08-21 (siebter Durchgang) — Test-Orakel komplett: Differential-Runner, 13-Frame-Korpus, Wire-Format-Dokument

Alle drei offenen Orakel-Punkte umgesetzt (Freigaben vom Benutzer: Umfang
"alles inkl. BLE", Sprache Englisch, Testverkehr ueber MCProxy+Bench erlaubt):

- **Differential-Runner (`a14eaada`):** `test_aprs_corpus` als Snapshot-Fence —
  kanonische decodeAPRS()-Ausgabe je Korpus-Frame gegen eingecheckte
  `golden.txt`, Regeneration nur bewusst per `APRS_GOLDEN_UPDATE=1`, plus
  Roundtrip decode→encode→decode. `pio test -e native_aprs` 5/5.
- **Korpus:** 13 on-air-Frames, Traffic-Mix gezielt erzeugt (MCProxy
  `POST /api/send` ueber die Produktions-Node, Bench-Heltec-DMs/Pos/HEY,
  echter Mesh-Verkehr). Abgedeckt: alle Frame-Typen inkl. DM mit `{NNN`,
  Text-ACK und kompaktem 12-Byte-Binaer-ACK. **Neufund dabei:** die
  0x41-Binaer-ACKs sind entgegen erster Annahme on-air (Layout
  `lora_functions.cpp:1078ff`) — der Korpus hat den Doku-Entwurf korrigiert,
  bevor er committet war.
- **Wire-Format-Dokument:** `docs/architecture/11-wire-format.md` (englisch,
  fuer Mock-Services von mc-chat/MCProxy/mcmap): LoRa-Frame byte-genau mit
  annotiertem Real-Beispiel, Server-UDP (KEEP/DATA-36-Byte-Header,
  GATE/CONF-TLV/BEAT), EXTUDP-JSON, BLE-Phone-Protokoll (GATT-UUIDs,
  `@`-Notifications, Kommando-Frames 0x10..0xF0; Querabgleich gegen MCProxys
  `ble_protocol.py`). Ehrliche Luecken markiert (Hello-Handshake, 0x44-JSON).
- mc-chat (`rpizero.local`) erreichbar, `/api/send` aber hinter Auth —
  aktiver mc-chat-Verkehr nicht erzeugt; Interlink-Frames kamen passiv
  ueber den Server-Pfad herein.
- Abschluss: Capture-Flag entfernt, RAK mit normalem Build geflasht
  (Gateway+EXTUDP on am OE-Server), alle Builds gruen, native 15/15,
  native_aprs 5/5.

### 2026-08-21 (sechster Durchgang) — `N-22` gefixt: Stack-Overflow im Loop-Task, kein EXTUDP-Bug

Root cause gemessen statt geraten: `uxTaskGetStackHighWaterMark(NULL) == 0` am
tiefsten Punkt des Pfads `checkSerialCommand()` → `sendMessage()` → `sendExtern()` —
der 4-KB-Loop-Task-Stack (LOOP_STACK_SZ im Adafruit-Core, hart codiert) war
vollstaendig aufgebraucht, Nachbar-RAM wurde zerstoert, Crash Sekunden spaeter.
EXTUDP war nur der Ausloeser (schob die Pfadtiefe ueber die Kante); auch das
Nebensymptom "Datagramme kommen trotz rc=1 nie an" gehoerte dazu. Vorher per
Experiment ausgeschlossen: Peer-Verhalten (Crash auch gegen stillen Live-Listener
auf dem Mac), Socket-Lebenszyklus (begin/beginPacket/write/endPacket alle ok).

Fix (`9ce62aa0`, Muster `1951aa7d`): grosse Puffer des Pfads auf nRF52 in BSS —
`sendMessage()` 200+200+300 B (läuft auf nRF52 nur im Loop-Task, Aufrufer
auditiert), `checkSerialCommand()` 600 B. Watermark danach am selben Punkt
**248 Woerter (~1 KB) frei**; mehrere Nachrichten mit EXTUDP on ohne Crash,
JSON-Datagramme kommen beim Peer an. **Workaround aufgehoben: EXTUDP wieder on,
EXT IP wieder 192.168.68.64 — Original-Konfiguration vollstaendig
wiederhergestellt.** Merkposten fuer Upstream in der N-22-STATUS-Box: die 4 KB
sind fuer diese Firmware knapp; jeder zusaetzliche `printfdeb`-Frame im Pfad
kostet ~900 B Stack.

### 2026-08-21 (fuenfter Durchgang) — `N-20`-Soak bestanden, `DRY-21`+convBuffer+`CONC-16`-Rest gefixt, `N-22` neu, Test-Orakel Stufe 1

- **`N-20`-Soak-Test bestanden:** Kabel 12:01 gezogen, 12:06 gesteckt — kein Freeze,
  kein Reboot (Uptime lief durch), DHCP automatisch neu bezogen, KEEP/Webserver danach
  wieder aktiv. `N-20`-Hauptfix damit vollstaendig verifiziert.
- **`DRY-21` (Drift) gefixt (`07d1360f`):** ACK-Level 0x02 fuer eigene Nachrichten aus
  der ESP32-Kopie nach `nrf_eth.cpp` portiert (App sah auf nRF52-Gateways nie den
  vollen Bestaetigungs-Status); Debug-Zeile angeglichen. Zusammenlegung der beiden
  Dateien bleibt Upstream-Epic.
- **convBuffer-Ueberlesen gefixt (`623c4c0e`):** der im CONC-16-Commit dokumentierte
  Nebenbefund, beide Fundstellen (APRS-Laenge `msg_len-36` statt Gesamtlaenge; auf
  nRF52 war es bei `msg_len > 239` ein echtes Out-of-bounds-Read).
- **`CONC-16`-Rest gefixt (`9117c3c6`):** die "auf nRF52 nicht gelinkt"-Annahme des
  urspruenglichen Fixes war falsch — `sendUDP()` in `nrf52_main.cpp` las den UDP-Ring
  ungeschuetzt, waehrend `addUdpOutBuffer()` aus dem Timer-Task schreibt. Jetzt
  Snapshot unter Lock + Eviction-Guard, auf dem Gateway live verifiziert.
- **`N-22` neu (Katalog):** EXTUDP crasht den nRF52-Ethernet-Gateway reproduzierbar
  ~2–4 s nach jedem eigenen Nachrichtenversand (Soft-Reset via SoftDevice-Fault-
  Handler, `RESETREAS=0x4`). Per Toggle isoliert (`--extudp off` → stabil), root cause
  offen (eigene Session). **Workaround aktiv: `--extudp off` auf dem Bench-RAK** —
  EXTUDP war dort vorher an (`EXT IP 192.168.68.64`). Die Diagnose kostete mehrere
  Fehlzuweisungs-Runden (der Crash sah erst wie ein Fehler des frischen
  Snapshot-Codes aus; Breadcrumbs + Bisect auf HEAD + RESETREAS klaerten es).
- **`RESETREAS`-Bootzeile (`6003e90c`):** eine Zeile Boot-Log, die kuenftig sofort
  Absturz von Spannungsproblem unterscheidet.
- **Test-Orakel Ausbaustufe 1 (`f1802030`, doc 08 §4):** `MC_TEST_HOOKS`-Capture-Hook
  in `OnRxDone()` (Hex-Dump akzeptierter Frames, Gegenpol zum CRC_PAYLOAD-Dump);
  `[env:native_aprs]` kompiliert `decodeAPRS()`/`encodeAPRS()` nativ (Minimal-Shim
  `test/support/nrf52/WisBlock-API.h`); `test_aprs_decode` mit zwei on-air
  mitgeschnittenen Fremd-Frames als Interop-Vektoren (Sollwerte von Hand aus den
  Roh-Bytes). `pio test -e native_aprs` 3/3. Offen: Differential-Lauf (Mechanismus 1),
  breiterer Vektor-Korpus, Wire-Format-Dokument.

Abschluss-Zustand: RAK4631 mit normalem Build (ohne Capture-Hook) geflasht, Gateway
am OE-Server (KEEP, NTP, Webserver HTTP 200), EXTUDP off (N-22-Workaround). Alle
Builds gruen (wiscore -Werror, t_echo, heltec_t114, heltec_wifi_lora_32_V3), native
15/15, native_aprs 3/3.

### 2026-08-21 (vierter Durchgang) — Ethernet-Kabel am RAK, `N-20`-Hauptfix, Dual-Node-Funktest

Benutzer hat ein Ethernet-Kabel an den RAK4631 (RAK13800-Modul war die ganze Zeit
bestueckt!) und einen Heltec V3 als zweiten Bench-Node angeschlossen.

- **Heltec V3 eingerichtet:** aktueller Kampagnen-Build geflasht (vorher Juli-Build
  mit Restkonfiguration als `DK5EN-98` — Rufzeichen-Duplikat zur Produktion!),
  Rufzeichen `DK5EN-91` gesetzt, `--loradebug on`, WLAN ORBI63 verbunden
  (192.168.68.69, Webserver on, **Gateway bewusst off** — die Produktion `DK5EN-98`
  bleibt der einzige Cloud-Gateway). Achtung Bench-Eigenheit: jedes Oeffnen des
  seriellen Ports resettet den Heltec (CP2102-Autoreset); Kommandos erst nach
  `CLIENT STARTED` senden.
- **Dual-Node-Funktest** (Gruppe `9999` + DM-Matrix an -12/-90/-91/-98/-99):
  Gruppen-Nachrichten beider Nodes mit Gateway-ACK; DMs an `DK5EN-98` direkt
  geackt, an `DK5EN-99` via Mesh-Relay ueber -98 geackt, `DK5EN-12` offline (kein
  ACK, erwartbar). Bench-Paar hoert sich mit −18…−20 dBm (Saettigung — die einzige
  Anomalie, ein nicht dekodiertes ACK 91→90-Richtung, ist damit erklaerbar);
  `DK5EN-98` bei −54…−59 dBm; Fern-Nodes (DL2JA-2, OE1XAR-33) −109…−112 dBm.
  Beim ersten Anlauf war der RAK-Loop komplett eingefroren (N-20, Gateway-Config
  ohne Link) — Zeitstempel stand, TX-Ring 19/20 voll, das erzeugte ACK an -91
  wurde nie gesendet.
- **`N-20`-Hauptfix (`780df254`):** `startETH()` prueft den Link (begrenzt 3 s
  wartend, PHY-Aushandlung nach HW-Reset!) VOR dem blockierenden
  `Ethernet.begin(10 s)`; der periodische Reconnect nutzt `resetDHCP()` statt
  `initethDHCP()` (das alte volle HW-Init resettete den PHY bei jedem Versuch —
  mit Link-Check waere das eine Endlosschleife gewesen, auf Hardware beobachtet
  und behoben). Verifiziert ohne Link (skip in <=3 s, Loop responsiv) und mit
  Link (DHCP-IP 192.168.68.68, KEEP-Heartbeats zum OE-Server, NTP-Sync,
  Webserver HTTP 200). **Offen: Soak-Test Kabel ziehen/stecken im Betrieb.**
- **Kosmetik-Fix (`ec033cb8`):** `Ethernet.localIP()` wurde als Roh-Integer
  geloggt ("1145350336"), jetzt dezimal.
- RAK-Konfiguration wieder im Originalzustand: Gateway on, EXTUDP on,
  Webserver on — als Gateway am OE-Server-Backend verbunden.

### 2026-08-21 (dritter Durchgang) — `N-21` aufgeklaert: kein USB-Bug, sondern eingefrorener Loop-Task (= `N-20`)

Auftrag: "investigate: the CDC host→board direction died mid-session while
board→host kept streaming". Ergebnis nach einem Tag Hardware-Instrumentierung
(acht Firmware-Iterationen auf dem RAK4631, jeweils per Serial-DFU geflasht):

- **USB vollstaendig entlastet.** EP0 (Touch), Line-State (DTR kam an, `ls=0x03`
  im "toten" Fenster), Bulk-OUT (Bytes erreichten den FIFO), Suspend-Flag (`sus=0`),
  TX-FIFO (`awr=256` — leer, es schrieb niemand mehr): alle Schichten einzeln auf der
  Hardware geprueft, alle gesund.
- **Tatsaechliche Ursache: der Loop-Task friert ein** — per Herzschlag-Breadcrumbs
  (Marken je Loop-Abschnitt, Freeze-Meldung aus dem Timer-Task ueber rohes
  `tud_cdc_n_write`) auf zwei Abschnitte eingegrenzt: Gateway-Block
  (`getUDP()`/`sendUDP()`, ≥20 s) und posinfo/heyinfo/telemetry (>2 min am Stueck).
  Beide enden in W5100S-Socket-/SPI-Operationen der RAK13800-Bibliothek — auf diesem
  Gateway-konfigurierten Board ohne Ethernet-Hardware/Link liefern SPI-Reads Muell,
  die Statusschleifen der Bibliothek kehren nichtdeterministisch nicht zurueck.
  "RX tot, TX lebt" = `checkSerialCommand()` laeuft nicht mehr, waehrend die
  `OnRxDone`-Debugzeilen aus dem Timer-Service-Task weiterstroemen. `N-21` ist damit
  als Duplikat von `N-20` geschlossen; `N-20` auf High hochgestuft und mit den
  Fundstellen praezisiert (beide STATUS-Boxen im Defektkatalog neu geschrieben).
- **Ein echter Fix committet (`1855cb3e`):** die printfdeb-Familie blockiert nicht
  mehr endlos in `Adafruit_USBD_CDC::write()`, wenn der TX-FIFO voll ist und nicht
  abfliesst (begrenzte 20-ms-Wartezeit, dann verwerfen; printfdeb schreibt chunked
  nur noch, was der FIFO frei hat). Das war der Verstaerker, der aus einem
  Loop-Freeze zusaetzlich einen Timer-Task-Freeze machte ("voll stumm").
- **Zwei teure Lektionen, im Katalog festgehalten:** (1) "seriell tot bei lebendem
  USB-Deskriptor" zuerst als Loop-Freeze pruefen, nicht als USB-Problem — ein
  Herzschlag-Breadcrumb kostet Minuten, die USB-Schichten-Forensik kostete Stunden.
  (2) LittleFS-Schreibzugriffe aus dem Timer-Service-Task crashen das Board
  reproduzierbar in einen Boot-Loop (waehrend der Untersuchung selbst ausgeloest;
  Rettung per Touch im Boot-Fenster) — Dateisystem nur aus dem Loop-Task.
- Der W5100S-Blockade-Fix selbst steht noch aus (Fix-Richtungen im Katalog unter
  `N-20`); die gesamte Diagnose-Instrumentierung wurde nach der Untersuchung wieder
  entfernt (git checkout), es verbleibt nur der printfdeb-Fix.
- Zweites Board (Heltec V3, DK5EN-91, `/dev/cu.usbserial-0001`) steht seit heute am
  Bench bereit, wurde fuer diese Diagnose aber nicht mehr gebraucht.

Abschluss-Zustand: RAK4631 geflasht (sauberer Stand + printfdeb-Fix), Boot sauber,
Rufzeichen DK5EN-90 intakt, `--info` beantwortet. Builds wiscore_rak4631 (-Werror),
t_echo, heltec_t114, heltec_wifi_lora_32_V3 SUCCESS; `pio test -e native` 15/15.

### 2026-08-21 (zweiter Durchgang) — `esp32-safeboot` gefixt, `N-19` gefixt, `N-20`/`N-21` neu

Zwei Auftraege: den vorbestehend kaputten `esp32-safeboot`-Build reparieren und den
`--dfu`-Haenger (`N-19`) auf der angeschlossenen Hardware untersuchen. Beides erledigt,
zwei neue Befunde dokumentiert:

- **`esp32-safeboot` gefixt (`e0f28bef`)** — kein Firmware-Bug, sondern
  Paket-Ping-Pong: Tasmota-Fork und Mainline-`espressif32` teilen sich das
  `framework-arduinoespressif32`-Verzeichnis; nach jedem Mainline-Build stuerzte der
  naechste Tasmota-Build vor dem Compile ab (`TypeError ... NoneType` in
  `arduino.py:555`, `get_package_dir()` liefert `None`) und heilte sich erst beim
  zweiten Aufruf. Deterministisch reproduziert, Fix per pre-Skript
  `tools/ensure_tasmota_framework.py` in beiden Safeboot-Envs (raeumt das fremde
  Verzeichnis weg und installiert das Tasmota-Paket vor dem Builder). Alle vier
  Flip-Richtungen verifiziert. **Keine Hardware noetig** — die Binaries selbst sind
  unveraendert (`safeboot.bin`/`safeboot-s3.bin` nach den Verifikationsbuilds per
  `git checkout` zurueckgesetzt).
- **`N-19` gefixt (`b03b9a27`)** — `--dfu` haengt nicht mehr. Eingrenzung per
  Ausschluss auf Hardware: `--reboot` (blankes `NVIC_SystemReset()` aus demselben
  Loop-Pfad) funktioniert, der 1200-Baud-Touch (`reset_mcu()` aus dem TinyUSB-Task)
  funktioniert — verdaechtig bleibt `sd_softdevice_disable()` im Loop-Kontext. Fix:
  GPREGRET per SoftDevice-SVC setzen (`sd_power_gpregret_clr/set(0, 0x57)`) und in den
  bewaehrten `NVIC_SystemReset()` durchfallen; `Serial.flush()` + `delay(300)` vor dem
  Reset ist funktional noetig (ohne Wartezeit bootete das Board trotz korrektem
  GPREGRET-Readback in die App). Zweimal in Folge verifiziert: `--dfu` → Bootloader-PID
  `0x29` + `/Volumes/RAK4631`; Rueckweg per `firmware.uf2`-Kopie → App laeuft. Der
  komplette Fern-Flash-Zyklus ohne physischen Zugriff ist damit bewiesen.
- **`N-20` neu** — die Ethernet-Init blockiert auf Gateway-Nodes ohne Link
  nichtdeterministisch: einmal blieb das Setup **minutenlang** nach
  `Initialize Ethernet` stehen (Loop nie gestartet, Fernrettung per 1200-Baud-Touch),
  und im Betrieb wiederholt sich die Init alle ~60–70 s aus der Loop und blockiert sie
  je 8–12 s (`RX_TIMEOUT_FIRE delta=12426…13920` statt `4582`). Die Aussage vom
  2026-08-21-Vormittag, der Ethernet-Fehlpfad "haengt nicht", ist damit ueberholt.
  Nicht gefixt (Details: `08-defect-catalogue.md`).
- **`N-21` neu** — die Host→Board-Richtung der USB-CDC starb im Test binnen ~1 min nach
  einem erfolgreichen `--info`, waehrend Board→Host weiterlief; mit drei Sendemethoden
  belegt. Vorbestehende Symptomklasse (motivierte `--dfu` urspruenglich). Nicht
  gefixt. Praktische Folge fuer Bench-Arbeit: wenn CDC-RX tot ist, bleibt der
  1200-Baud-Touch der verlaessliche Fernweg in den Serial-DFU-Bootloader (heute zweimal
  als Rettung genutzt), danach `pio run -e wiscore_rak4631 --target upload`.
  _(Ueberholt: am selben Nachmittag als Duplikat von `N-20` aufgeklaert — kein
  USB-Defekt, der Loop-Task war eingefroren; siehe den Abschnitt zum dritten
  Durchgang.)_

Verifikation des Durchgangs: `wiscore_rak4631` (mit `-Werror`), `t_echo`, `heltec_t114`
SUCCESS; `pio test -e native` 15/15; Hardware nach Abschluss gesund (LoRa-RX/TX laufen).

### 2026-08-20/21 — nRF52-Konkurrenz-Durchgang auf `wiscore_rak4631`

Mit angeschlossener Hardware, je ein Commit, je gebaut (`pio run` ueber alle 32 Envs
gruen bis auf das vorbestehend kaputte `esp32-safeboot` — am 2026-08-21 gefixt,
`e0f28bef`), `pio test -e native` gruen:

- ~~`N-16`~~ (`cc79611b`) — `Radio.Send()` in `doTX()` (drei Fundstellen) von
  `taskENTER_CRITICAL()` auf `vTaskSuspendAll()`/`xTaskResumeAll()` umgestellt: der Guard
  sollte vor dem FreeRTOS-Timer-Service-Task schuetzen (echter Task-Kontext, keine ISR),
  aber `taskENTER_CRITICAL()` friert dabei den Tick ein, den `SX126xWaitOnBusy()`s
  `delay(1)`-Schleife zum Zurueckkehren braucht.
- ~~`CONC-17`~~ (`bb97b87c`) — `settings_rx_callback()` kopiert nicht mehr live in
  `meshcom_settings`; staged in einen privaten Puffer, `applyPendingBleSettings()` wendet
  die Kopie einmal pro `nrf52loop()`-Durchlauf unter kurzem Lock an.
- ~~`N-04`-Restbefund~~ (`6268667a`) — `blelen==0` in `sendToPhone()`/`sendComToPhone()`
  fuehrte zu `uint8_t`-Unterlauf; abgefangen.
- ~~`CONC-15`~~/~~`CONC-18`~~ (`ed9116f6`) — `addBLEOutBuffer()` schreibt Ring-Slot und
  Index unter Lock; `sendToPhone()` snapshot't Laenge/Status/Payload in einem Rutsch statt
  spaeter erneut aus dem live Ring zu lesen. `sendComToPhone()` bewusst nicht angefasst
  (Schreiber und Leser laufen bereits beide im Main Loop, keine Nebenlaeufigkeit).
- ~~`CONC-16`~~ (`ca574ef7`) — dieselbe Behandlung fuer `udpWrite`/`udpRead`. Auf
  `wiscore_rak4631`/`heltec_t114` per `--gc-sections` nicht gelinkt (Gateway/UDP-Pfad dort
  nicht erreichbar) — nur auf ESP32 tatsaechlich verifizierbar, dort sind die neuen Locks
  No-ops. Auf echter nRF52-Hardware mit Ethernet ungeprueft (`DRY-21`).
- ~~`DRY-22`~~ (`9b6c5224`) — `checkSerialCommand()`-Drift (NUL-Byte-Schutz,
  Self-Healing-Invarianzpruefung) von ESP32 nach nRF52 portiert.
- ~~`N-15`~~ — kein Code-Fix noetig, bereits durch `CONC-14` (2026-08-18) geschlossen; nie
  re-verifiziert, jetzt nachgeholt.

**Auf Hardware getestet, 2026-08-21 — alle sieben Commits verifiziert.** `wiscore_rak4631`
angeschlossen, geflasht (`Device programmed.`), Rufzeichen/BTCODE/Settings haben den Flash
ueberlebt. Ueber ~90 s Laufzeit beobachtet:

- Boot sauber, kein Boot-Loop, BLE initialisiert (`MC-690d-DK5EN-90`), kein Ethernet-Kabel
  (erwartetes `Failed to configure Ethernet` bleibt ohne Folgen).
- Reale LoRa-RX empfangen, dedupliziert, ueber `RELAY_QUEUED`/`RING_WRITE` in den TX-Ring
  gestellt.
- Reale LoRa-TX gesendet (`CAD_SCAN` → `TX-LoRa` → `OnTXDone` → zurueck nach
  `RX_LISTEN`) — **durchlaeuft direkt den N-16-Fix** (`Radio.Send()` unter
  `vTaskSuspendAll()`), zweimal beobachtet (einmal vor, einmal nach dem `--dfu`-Vorfall
  unten), beide Male sauber ohne Haenger.
- `--info` ueber Seriell beantwortet korrekt (Rufzeichen, Batterie 4.25 V/100 %,
  Flash-Version, Frequenz) — **durchlaeuft den DRY-22-Fix**
  (`checkSerialCommand()`).
- `CONC-17`/`CONC-15`/`CONC-18`: kein Crash/Hang ueber die gesamte Laufzeit; direkte
  BLE-Settings-Schreib-Verifikation (Telefon-App) nicht durchgefuehrt, keine App zur Hand.
- `CONC-16`: auf `wiscore_rak4631` nicht gelinkt (siehe Commit-Notiz), daher auf diesem
  Board nicht pruefbar.

> **Neuer Befund — `--dfu` haengt sich auf, statt in den UF2-Bootloader zu wechseln.**
> `--dfu` gesendet (Befehl aus `7bac915a`, gestern committet, dort selbst als "auf
> Hardware noch ungetestet" vermerkt). Ergebnis: die serielle Konsole verstummte
> vollstaendig (fuenf verschiedene Leseversuche ueber ~50 s: `cat`, `stty`+`cat`,
> `pio device monitor`, `pyserial` mit gesetztem DTR/RTS, verlaengertes passives
> Lauschen — durchweg 0 Bytes), aber der USB-Deskriptor zeigte weiterhin die
> Applikations-PID `0x8029` (`ioreg`), nie die Bootloader-PID `0x0029`, und
> `/Volumes/RAK4631` erschien nie. Weder normaler Betrieb noch Bootloader — das Board war
> haengengeblieben. Ein einzelner Tastendruck auf Reset (durch den Benutzer, kein
> physischer Zugriff meinerseits) hat es vollstaendig zurueckgeholt: wieder
> Applikations-PID, serielle Konsole sofort wieder aktiv, RX/TX/Settings unveraendert
> intakt — kein Datenverlust, kein Soft-Bricking.
>
> `enterUf2Dfu()` (`cores/nRF5/wiring.c:98`) ruft `sd_softdevice_disable()` vor
> `NVIC_SystemReset()` auf; ob genau das der Haenger-Punkt ist, ist nicht verifiziert —
> nur das Symptom (kein Reset, kein Bootloader, stumme Konsole) ist belegt. Der
> aufrufende Codepfad in `nrf52loop()` (`bEnterDfu`-Zweig, `rebootAuto`) liegt ausserhalb
> jeder in dieser Session neu eingefuehrten Critical Section — der Fund ist also nicht
> durch die heutigen Aenderungen verursacht, sondern ein vorbestehender, jetzt zum ersten
> Mal beobachteter Bug in gestrigem `7bac915a`. **`--dfu` bis zur Untersuchung nicht
> verwenden** — physischer Reset bleibt der einzige verifiziert funktionierende Weg in
> den Bootloader diese Session. _(Ueberholt: am Nachmittag desselben Tages gefixt,
> `b03b9a27` — siehe den Abschnitt zum zweiten Durchgang.)_

### 2026-08-20 — Wave 0.4/0.2 auf wiscore_rak4631, `CFG-01` neu gefunden

`nordicnrf52` gepinnt (alle drei nRF52-Boards), `-Werror` fuer `wiscore_rak4631` aktiviert
(20 echte src/-Warnungen vorher behoben, siehe Commit `a3a30ef0`). Der Versuch, dasselbe fuer
`heltec_t114`/`t_echo` zu tun, deckte einen eigenstaendigen, groesseren Befund auf:

> **`CFG-01` — `[nrf52]`-Sektion kollidiert namensgleich ueber alle drei
> `variants/*/platformio.ini`.** `platformio.ini:14` laedt sie per
> `extra_configs = variants/*/platformio.ini`; jede der drei Dateien deklariert ihre
> eigene `[nrf52]`-Sektion mit demselben Namen. Solange nur `wiscore_rak4631`s Sektion
> zusaetzliche Keys (`build_flags`, `build_src_flags`) trug, gewann sie fuer alle drei
> Envs — `heltec_t114` und `t_echo` bekamen dadurch **`BOARD_RAK4630` in ihre Compile-Flags
> gemischt**, obwohl sie das WisBlock-RAK-Board gar nicht sind. Das blieb unbemerkt, weil
> `#ifndef BOARD_RAK4630` an drei Stellen (`adc_functions.cpp`, `batt_functions.h`,
> `batt_function_old.cpp`) als Proxy fuer "ist ESP32" benutzt wurde — mit dem geleakten
> Define kam dort nie echter ESP-IDF-Code (`esp_adc_cal.h`) zum Tragen. Sobald
> `heltec_t114`/`t_echo` eine eigene, isolierte `[nrf52]`-Sektion bekommen (z.B. durch
> Hinzufuegen von `build_flags`, wie im `-Werror`-Versuch geschehen), verschwindet das
> geleakte Define, und die drei Stellen versuchen echten ESP-IDF-Code auf nRF52 zu
> uebersetzen → Build bricht.
>
> **Behoben (Commit `b0c3d8c0`):** die drei Guards von `BOARD_RAK4630` auf die tatsaechliche
> Plattform umgestellt (`defined(ESP32)` bzw. `!defined(NRF52_SERIES)`), dabei auch einen
> zweiten, unabhaengigen Bug gefunden — `batt_function_old.cpp:69` nahm `BOARD_T_ECHO`
> bereits explizit aus dem ESP-IDF-Typenblock aus, aber nie `BOARD_HELTEC_T114` (jemand
> hatte das Problem fuer `t_echo` schon einmal von Hand gepatcht, nur unvollstaendig).
> Damit ist der **Symptom-Level** (die drei falschen Guards) gefixt und fuer alle Boards
> verifiziert bit-identisch zum vorherigen Verhalten.
>
> **Root cause NICHT behoben** — die `[nrf52]`-Sektionskollision selbst steht noch offen.
> Sie ist gefaehrlich, weil sie sich nicht auf diese drei Stellen beschraenkt: JEDE
> zukuenftige Aenderung an einer Sektion `[nrf52]`/`[esp32]` in einer `variants/*/platformio.ini`
> kann auf dieselbe Art in andere Boards derselben Familie durchsickern, abhaengig davon,
> welche Datei zuletzt geladen wird und welche Keys sie definiert — nicht offensichtlich aus
> dem Diff einer einzelnen Datei ersichtlich. Sauberer Fix waere, jede Sektion eindeutig zu
> benennen (`[nrf52_rak4631]`, `[nrf52_t114]`, `[nrf52_techo]`) und `extends` entsprechend
> anzupassen — angefasst nur mit allen drei Boards am Bankarbeitsplatz, nicht heute.
>
> **Trigger fuer erneutes Aufgreifen:** sobald `heltec_t114` oder `t_echo` an Hardware
> verfuegbar sind (fuer Wave 0.2-Rest dort), oder bei der naechsten Aenderung an einer
> `[nrf52]`/`[esp32]`-Sektion in `variants/*/platformio.ini`.

## 4. State of the repository

### 4.1 Branch model (decided 2026-08-29)

```
upstream/dev --merge--> v4.35p_prio (fork main) --branch--> topic branch (tdeck-...)
      ^                        |                                  |
      |                        |<------------- merge back --------+
      +---- PR <-- pr/<topic> <+  (built from upstream/dev + firmware files only, 1 commit)
```

- **`v4.35p_prio` is fork main and the permanent home of everything**: docs, `tools/`, `test/`,
  `src/instrument.*`, `src/test_inject.*`, `src/t-deck/tdeck_debug.*`, the bench harness. It
  tracks upstream by **merge, never rebase** (icssw-org squash-merges; a rebase turns our merged
  fixes into deletion-only commits — memory `merge-not-rebase-after-upstream-squash`).
- **Topic branches** off fork main for active work; merged back when done, then deleted.
- **PR branches are built, not branched**: `git checkout -b pr/<topic> upstream/dev`, then
  `git checkout v4.35p_prio -- <firmware files>`, cut the four couplings (memory
  `firmware-only-pr-coupling`), squash to one commit with the German per-file description. No
  docs, tools, tests or debug code in a PR. After upstream squash-merges, `git merge upstream/dev`
  into fork main sees identical content and the surrounding debug hooks survive.
- **Debug code rule:** new instrumentation goes into the dedicated files above with one-line
  hooks in production code — that keeps the conflict surface with upstream refactors minimal.
  For v5 the durable path is to offer the instrumentation upstream as a default-off compile
  option in its own PR once the T-Deck PR has landed.

### 4.2 Branches as of 2026-08-29

| Branch                                             | State                                   | Decision                                                          |
| -------------------------------------------------- | --------------------------------------- | ----------------------------------------------------------------- |
| `v4.35p_prio`                                      | fork main, +204/-14 vs `upstream/dev`   | keep                                                              |
| `tdeck-partial-refresh-trace`                      | fork main + 7 T-Deck commits            | keep until merged back                                            |
| `tdeck-partial-refresh-wip`                        | superseded, local only                  | tag `archive/tdeck-partial-refresh-wip-20260828`, delete          |
| `pr/firmware-only`, `pr/fwdate-buffer`             | merged upstream (#1102, #1103), ahead 0 | delete local + origin (GitHub keeps `refs/pull/*`)                |
| `backup/v4.35p_prio-pre-rebase*-20260827`          | pre-rebase snapshots, local only        | tags `archive/pre-rebase-20260827{,-2}`, pushed, branches deleted |
| `origin/claude/flood-network-priority-send-5m073l` | all commits in fork main by content     | tag `archive/claude-flood-network-20260822`, delete               |
| `master`, `gh-pages`                               | fork mirror / pages                     | ignore                                                            |

Archive tags are pushed to `origin`; nothing is deleted before its tag exists on the remote.

## 5. Where to read what

| Question                                              | Document                                              |
| ----------------------------------------------------- | ----------------------------------------------------- |
| Is it spaghetti? Where is the kernel?                 | `docs/architecture/01-system-overview.md`             |
| How do 30 variants map onto one tree?                 | `docs/architecture/02-build-and-variants.md`          |
| What is outdated and what breaks?                     | `docs/architecture/03-dependencies.md`                |
| Where is the code unmaintainable?                     | `docs/architecture/04-complexity-and-duplication.md`  |
| Should we rewrite?                                    | `docs/architecture/05-rewrite-vs-refactor.md`         |
| Which test layers, and why                            | `docs/architecture/06-test-strategy.md`               |
| What can be observed/driven; bench design             | `docs/architecture/07-verification-infrastructure.md` |
| **What is broken, in what order, with what evidence** | `docs/architecture/08-defect-catalogue.md`            |
| Which core/task touches which state (goal G8)         | `docs/architecture/09-concurrency-map.md`             |
| Every buffer, its size and its bounds (goal G7)       | `docs/architecture/10-buffer-inventory.md`            |
| Wire format: LoRa / server UDP / EXTUDP / BLE         | `docs/architecture/11-wire-format.md`                 |
| The 39 pre-existing findings                          | `docs/code-audit-20260712.md`                         |
| Raw evidence behind 08/09/10                          | `docs/review/2026-07-31/`                             |

> **Read `08` before acting on `01`–`07`.** Those were written before the adversarial
> review and carry correction boxes pointing at `08 §1`.

`docs/review/2026-07-31/` holds the nine unedited reports from the review that produced
`08`, `09` and `10` — eight independent finder angles plus the reconciliation against
`docs/code-audit-20260712.md`. They are archived so every claim in the distilled documents stays
traceable to its source, and so a later session can see what was examined and found
_harmless_ without re-deriving it. They are a snapshot of 2026-07-31 and were written
before the rebase: **their line numbers are stale by construction.** Treat the distilled
documents as current and these as provenance.

## 6. Known gaps in this documentation set

Recorded so they are not mistaken for completeness.

| Gap                                                                                                                                                                                               | Impact                                                              |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| ~~No wire-format specification.~~ **Closed 2026-08-21:** `docs/architecture/11-wire-format.md` covers LoRa, server UDP, EXTUDP JSON and BLE; spec-derived vectors exist (`test/test_aprs_spec/`). | —                                                                   |
| **No persistence/flash-migration document.** `FLASH_VERSION` does not migrate, and there are two incompatible `meshcom_settings` layouts (`N-12`).                                                | A field change to the settings struct is currently unsafe on nRF52. |
| **No boot/OTA document.** One `ota_0` slot means no rollback by construction; five boards have no remote update at all.                                                                           | Unknown recovery path after a bad update.                           |
| Existing German design docs (`docs/README_LORA_TRX.md`, `docs/adr-*.md`, `docs/prio-talk-flood-networking.md`, ~180 KB) are not linked from this set.                                             | Duplication risk; some of it already answers questions asked above. |
| `tools/ram_snapshot.py` hardcodes 7 targets, so "RAM baseline across all envs" is not executable as written (`08` C-12).                                                                          | Baseline is partial.                                                |
