# Resume — MeshCom Firmware Hardening Campaign

Working document for picking the campaign back up. Records **what we set out to do**,
**how we decided to get there**, and **exactly where we stand**.

Last updated 2026-08-18. Branch `v4.35p_prio`, rebased onto `upstream/dev`.

> ### Standing risk — read first
>
> **Status 2026-08-18.** Of the eight RF- or network-reachable defects, **four are now fixed on
> this branch** — `N-03` (CONF zero-fill), `N-04` (BLE length underflow), `N-05` (mheard heap
> over-read), `N-06` (web `t_io` index) — plus `SEC-02` (format string). None are upstream yet,
> so **the shipped fleet still runs all of them**.
>
> **Three remain deliberately unfixed**, accepted by maintainer decision on 2026-08-18:
> `N-01` (`{MCP}` password bypass), `N-02` (`{SET}` unauthenticated routing change) and `N-07`
> (BLE command channel unauthenticated). `N-01`/`N-02` are accepted as risk. `N-07` because the
> effective fix is BLE bonding, which disconnects every existing phone app until the user
> re-pairs — an upstream decision, not a branch decision. Rationale in
> [`docs/architecture/08-defect-catalogue.md` §2](docs/architecture/08-defect-catalogue.md).
>
> Getting the four fixes upstream is now the highest-value open item in this campaign.

---

## 0. Re-entry procedure

Do this **in order** before touching anything. Skipping step 2 is the trap that already
cost one wrong conclusion in this campaign.

1. **Orient.** Read this file, then
   [`docs/architecture/08-defect-catalogue.md`](docs/architecture/08-defect-catalogue.md).
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
[`docs/architecture/08-defect-catalogue.md` §1](docs/architecture/08-defect-catalogue.md);
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
> Authoritative: [`docs/architecture/09-concurrency-map.md`](docs/architecture/09-concurrency-map.md).

**Prior art the first concept missed entirely:** `fable-verdict.md` (repo root, 2026-07-12)
holds **39 already-verified findings** (`SEC-01` … `TEST-39`). Essentially all are still
open. The campaign adopts those IDs rather than re-deriving them.

### 2.3 Answers to the open questions

**G5 — should we rewrite 1:1?** **No.** Reasoning in
[`docs/architecture/05-rewrite-vs-refactor.md`](docs/architecture/05-rewrite-vs-refactor.md).
Short version: the tests needed to validate a rewrite must exist _before_ the rewrite, so
the first work item is identical either way — and once the harness exists, incremental
work is strictly cheaper and shippable. The corrected reasoning (per C-02) is that the
blocker between the two MCU stacks is a differing **concurrency model**, not a differing
API, which a rewrite would not resolve either.

**The replacement oracle (after C-03 killed golden vectors):**

1. **Differential testing** — compile pre-fix and post-fix logic into one native binary and
   assert they agree. A true before/after comparison needing no external oracle.
2. **A real capture path** — add a hex dump of _accepted_ frames behind `MC_TEST_HOOKS`
   (today only CRC-failed frames are dumped), then re-capture on the bench.
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
| Concurrency / core map (G8)     | see §4     | [`09-concurrency-map.md`](docs/architecture/09-concurrency-map.md) — **13 correctness-affecting races, 9 benign, 5 correctly protected, 16 over-synchronised, 3 dead**. Supersedes the earlier 9/4/14 figures.                                                                                                                                                                                                                                                                                                                                                                                                              |
| Buffer/type audit (G7)          | see §4     | [`10-buffer-inventory.md`](docs/architecture/10-buffer-inventory.md) — 2 critical, 3 high, 1 medium-high, 11 medium, 12 low(-medium), 2 already fixed; type findings T3-1…T3-9                                                                                                                                                                                                                                                                                                                                                                                                                                              |
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
| **0.2** `-Werror` fuer `src/`   | (this)     | `-Wall -Wextra` galt schon; 3 `src/`-Warnungen behoben (`Regexp.cpp` `-Wclobbered` via `volatile`, `net_console.cpp` `-Wmisleading-indentation`), dann `-Wformat=2 -Wno-missing-field-initializers -Werror` auf `build_src_flags` im `[esp32]`-Block. 22/23 ESP32-Envs SUCCESS (`esp32-safeboot` vorbestehend defekt, erbt die Flags nicht). nRF52 offen bis Hardware da ist                                                                                                                                                                                                                                                |

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

| #          | Step                                                                                                                                                                                                                                                                                                                                                                       | Refs            |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- |
| 0.4        | Pin `nordicnrf52` to an exact version (currently unpinned → CI and local build against different cores)                                                                                                                                                                                                                                                                    | doc 02 B-04     |
| 0.2 (Rest) | **ESP32-Kern DONE** 2026-08-18 (23 Envs mit `extends = esp32`, `-Wformat=2 -Wno-missing-field-initializers -Werror` auf `build_src_flags`). Offen bleiben die Envs ohne `extends = esp32`, denen schon `-Wall -Wextra` fehlt: `t_deck_pro`, `t5_epaper`, `vision-master-e213`, `vision-master-e290`, `wireless-paper` — plus die drei nRF52-Boards, sobald Hardware da ist | doc 08 Wave 0.2 |
| ~~0.3~~    | **DONE** 2026-08-18 — zwoelf Guards auf `defined()` (zwei davon nur ueber `-Wundef` gefunden, `aht20.cpp` ist ISO-8859 und wird von `grep` ohne `-a` uebersprungen). `-Wundef` NICHT im Build aktiviert: 10.317 Treffer, davon 2 aus `src/`. Regressionsschutz stattdessen als CI-Job `macro-guards`. `BOARD_*`-Stringmakros bewusst unangetastet.                         | `N-10`          |
| 0.6        | Extend the native suite: CSMA timing math, `via_functions`, `compress_functions` (the latter already surfaces two `-Wsign-compare` warnings)                                                                                                                                                                                                                               | doc 08 Wave 0.5 |

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

### 3.4 Then — Wave 2, remaining prior-verdict Track A

`CONC-15`–`CONC-18`, `N-14`–`N-16`.

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

---

## 4. State of the repository

- Branch `v4.35p_prio`, rebased onto `upstream/dev`, **0 commits behind upstream**.
- 5 campaign commits on top; `3fb2c917` (the test harness) is **not yet pushed**, the rest are.
- Working tree state: see `git status`. **This document, `docs/architecture/09` and `10`, and
  `docs/review/` are part of the deliverable — if they are untracked, commit them before
  any `git clean` or rebase.**
- Code divergence from upstream: ~195 insertions / 131 deletions across 17 files.
- `pio run` unaffected by the harness — `native` is deliberately not in `default_envs`.
- Pre-rebase safety SHA: `9a4dd1b1` (also still on `origin` history via reflog).

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
| The 39 pre-existing findings                          | `fable-verdict.md` (repo root)                        |
| Raw evidence behind 08/09/10                          | `docs/review/2026-07-31/`                             |

> **Read `08` before acting on `01`–`07`.** Those were written before the adversarial
> review and carry correction boxes pointing at `08 §1`.

`docs/review/2026-07-31/` holds the nine unedited reports from the review that produced
`08`, `09` and `10` — eight independent finder angles plus the reconciliation against
`fable-verdict.md`. They are archived so every claim in the distilled documents stays
traceable to its source, and so a later session can see what was examined and found
_harmless_ without re-deriving it. They are a snapshot of 2026-07-31 and were written
before the rebase: **their line numbers are stale by construction.** Treat the distilled
documents as current and these as provenance.

## 6. Known gaps in this documentation set

Recorded so they are not mistaken for completeness.

| Gap                                                                                                                                                   | Impact                                                                                                     |
| ----------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| **No wire-format specification.** The on-air protocol is named the highest-risk contract in five places and is written down nowhere.                  | Blocks spec-derived test vectors (§2.3 item 3) — the only kind that can catch a _pre-existing_ decode bug. |
| **No persistence/flash-migration document.** `FLASH_VERSION` does not migrate, and there are two incompatible `meshcom_settings` layouts (`N-12`).    | A field change to the settings struct is currently unsafe on nRF52.                                        |
| **No boot/OTA document.** One `ota_0` slot means no rollback by construction; five boards have no remote update at all.                               | Unknown recovery path after a bad update.                                                                  |
| Existing German design docs (`docs/README_LORA_TRX.md`, `docs/adr-*.md`, `docs/prio-talk-flood-networking.md`, ~180 KB) are not linked from this set. | Duplication risk; some of it already answers questions asked above.                                        |
| `tools/ram_snapshot.py` hardcodes 7 targets, so "RAM baseline across all envs" is not executable as written (`08` C-12).                              | Baseline is partial.                                                                                       |
