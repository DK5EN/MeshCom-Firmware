# Backlog — MeshCom Firmware Hardening Campaign

Working document for picking the campaign back up. Records **what we set out to do**,
**how we decided to get there**, and **exactly where we stand**. Open work lives in
§3.2; §3.1 is the done-list kept for evidence.

_(Previously `resume.md` in the repository root.)_

Last updated 2026-08-31 afternoon, at the push of `fb32f4ef`. The 3.8o/3.8p intake
campaign is fully implemented (Waves A/B/C via `/orchestrate-waves`: WEB-01..04, NC-01/02,
MH-02, TM-45, TM-39-CONF, BAT-01/02, CHR-01/02, JSN-01, TD-07/08, ETH-01, CONF-01, CTY-01,
PM-01, NTP-01, DOC-02; Doku PRES-01/02, DOC-01/03/04), gate 438 native cases / 12 envs,
7 standard targets build, bench proofs in the rows. Commits `81cfc064`, `b624bd33`,
`a1191eaa`, `361ac9fe`, `d4f875e9`, `d8054454` (+ docs). **PR-Entwurf für den
Upstream-Nachtrag: [`pr-draft-20260831.md`](pr-draft-20260831.md)** — offene
Schnittfragen dort in Teil E. 2026-08-31 nachmittags dazu: **GW-01 gefixt und belegt,
TM-06/07/14/19 erledigt** (Commits `2a8d1ef9`, `4f703e82`; TM-07 hat das Register benannt:
nur `GPSPI2.clock`). Abends: **Release v4.35p.08.31-stability publiziert** (Changelog-Items
107-152, 39 Assets, 5 seit 08.28 ungebaute Envs repariert — E22_XML-DRAM, 2x Safeboot,
t114/t_echo) und **die ganze Flotte geflasht** (OTA 93/92/98, USB 14 + RAK-90); dabei
TM-46/TM-47 gefunden und gefiled. Offen bleiben: TD-09, TD-10 (zurückgestellt,
[`tdeck-backspace-autorepeat-20260831.md`](tdeck-backspace-autorepeat-20260831.md)), E22-01, TLM-01..03, MEM-02
(geparkt), TM-44 (deferred), UDP-01-Rückfragen, APRS-01, WF-01 Sites 1+2, TM-28 (Hardware
ab 2026-09-01), WEB-03 (c)-(e). Aktuelle Übergabe: [`RESUME.md`](RESUME.md).

Last updated 2026-08-31 evening, after the GW-01 + TM-06/07/14/19 campaign
(`2a8d1ef9` wave 1: --injectraw/--loratx/--spitrace/--touch + harness scenarios;
`4f703e82` wave 2: GW-01 fix (a) bench- and interlink-proven, TM-07 register named
GPSPI2.clock, TM-14 measured, TM-19/06 bench-proven) and the pr-draft refresh.
Previous update 2026-08-30 night, at the push of `7b65233a` — the `/orchestrate-waves` intake
campaign shipped Waves 1-3 in five commits (`cea337ff` Wave 1, `5a903239` Wave 2, `861f2967`
MEM-01, `7403a97d` TM-43, `7b65233a` PT-01 fixes + UDP-02); the wave paragraphs below and the
§3.8f/h/j/k/l/m row statuses carry the detail, [`RESUME.md`](RESUME.md) the hand-over list.
Earlier the same day (WLAN-Bericht [`wifi-report-20260830.md`](wifi-report-20260830.md); §3.8f: TM-34 Wave W shipped — F1–F7 + WPA2-PSK on WPA2/WPA3 APs; **TM-35 async NTP** (loop max 314 → 145 ms, 0 gaps); **TM-31 UDP instrument + gateway relay fix** (0/30 → 30/30 queued) and upstream #568 answered; **TM-16 boot time** (T-Deck ready 14.9 → 10.9 s over 24 boots); **TM-11/TD-01 closed** 24/24; **HL-01..04** done; **TM-37 filed**). Branch `v4.35p_prio`, rebased onto `upstream/dev`; T-Deck work on `tdeck-partial-refresh-trace`.

**Wave 1 (2026-08-30 evening, `/orchestrate-waves`):** `RX-01`, `TX-01`, `FL-02`, `CS-01`, `CS-02`, `PT-01` (58 new native cases, 8 parser findings pinned), `TM-42` done and verified on hardware; `TM-38` runner shipped (`apreboot.py`, real run needs the operator at the AP). Native gate 346 cases / 9 envs, four boards built and flashed. **Wave 2 (same evening):** `BP-01`/`TM-37`, `CS-03`, `TM-39`, `TM-40`, `TM-41` done and verified on hardware (native 381 cases / 10 envs). New findings filed under the rows: CS-03 (E22-DevKitC DRAM headroom ~1.7 kB; `/config.json` hands out secrets whenever `node_webpwd` is empty), TM-39 (nRF52 `CONF` indicator unknown to ESP32; nRF52 internet path has no per-country case), plus the 8 PT-01 parser findings. **Wave 3 (same night):** `MEM-01` (DRAM guard + classic-ESP32 rings 20/20), `TM-43` (EXTUDP regression, RAK PASS 601 s soak), `UDP-01` stack question answered, `UDP-02` found + fixed, and all eight PT-01 parser findings fixed (native gate 390 cases / 10 envs, 0 skips).

**Intake 2026-08-30 (operator list of 11 points), filed:** configuration surface in §3.8h (`CS-01` max-hop over serial + NVRAM, `CS-02` web drop-down, `CS-03` config download/upload); what the central server learns in §3.8i (`GW-01` HEY parity with `--gateway on`, `TLM-01`/`TLM-02` telemetry definitions **parked**, `TLM-03` soft-serial telemetry review); tests in §3.8f (`TM-38` AP-reboot recovery, `TM-39` country servers, `TM-40` OTA regression, `TM-41` T-Deck colour/geometry display test, `TM-42` group `TEST`).

**Intake 2026-08-30 (second list), filed in §3.8j:** `FL-01` **beacon flood — fixed this session** (a node can be driven to originate position beacons at loop rate; the mcmap "replay burst" finding is a misreading, see the section), `PT-01` protocol-parser test coverage (six parsers have no test at all), `WF-01` trailing bytes on BLE/`KEEP` frames. Bench LoRa power set to 2 dBm on all four nodes (§3.8f bench table).

**Intake 2026-08-30 (third list), filed in §3.8k:** `RX-01` discard frames from unconfigured nodes (`XX0XXX`, seen relayed over four hops), `TX-01` an unconfigured node refuses to transmit at all (the other half of RX-01), `BP-01` TX back-pressure to the sender as Q-code notices (QRS/QRT/QTA, plus QRV once the queue clears — the concrete design for `TM-37`), `FL-02` the same 30 s floor for `sendHey()`. `CS-04` (Web-API `/getparam/`) **fixed and verified on hardware**; the two corrections to the mcmap replay-burst finding are written back into `mcmap/docs/findings/interlink-frame-replay-bursts.md` §10.

**Operator report 2026-09-01 (E22 on a fixed IP), filed in §3.8s:** "with a fixed IP no connection to the server, with DHCP no problem, and the node's web server is reachable either way" — plus, on being asked again, "it feels sluggish". Switching to a fixed IP silently swaps the resolver: the DHCP branch takes `WiFi.dnsIP()` from the router, the static branch takes `node_owndns` and falls back to the hard-coded literal **`8.8.8.8`** when it is empty (`NET-02`). The server is reached by name, so a resolver that cannot answer leaves `node_hostip` at `0.0.0.0` and the node emits **no** server traffic, while everything that needs no DNS keeps working. The operator cannot see any of it, because the setup page pre-fills the four network boxes with the node's **effective** values while their buttons write the **stored** ones — NTP is the only one of the five that is right (`NET-01`, the item that actually produced this ticket). On a fixed IP the node also reports itself online from its settings string rather than from the interface (`NET-03`), `--setowndns` has a dead duplicate handler with a wrong offset (`NET-04`), and the static address is only applied after a DHCP lease has been obtained (`NET-06`). The "sluggish" half is `NET-05`, blocking `hostByName()` with no retry — **already fixed here and upstream** by `f34fd2ae`, present in every released 4.35p build. All upstream, none of it ours. Root cause is established by code reading; the field case is **not yet confirmed** — two commands would settle it, doc §7. [`bug-static-ip-dns-20260901.md`](bug-static-ip-dns-20260901.md).

**Field report 2026-09-01 (`OE5HWN-14`), filed in §3.8r:** the reported symptom (WX altitude wandering 179–308 m on a node that never moved) is the smallest part of it. The GPS UART is drained only every 3 s on ESP32 while the L76K sends ~140 B/s into a 256-byte ring, so ~165 B of NMEA is discarded **every cycle**; roughly 1 in 256 of the resulting spliced sentences passes the checksum and is committed as a real fix (`GPS-01`, `GPS-02` — observed once in the 22-minute log, predicted 1.7). Altitude is a single unfiltered sample and `--setalt` is a no-op while GPS is on (`GPS-03`); QNH is latched to the first fix after boot (`GPS-04`). Side findings from the same logs: the node reset twice in seven minutes and **no reset reason is ever printed** (`TM-51`, do this first), and the display path costs ~570 ms per update on the T-Beam Supreme (`TM-52`). An HDOP-weighted Kalman filter was requested during review and is **rejected on measurements** (`GPS-05a`/`GPS-05b`, doc §7.6) in favour of a scalar Kalman filter with **constant** `R` — 8 bytes, whose steady-state gain _is_ an EMA coefficient, and whose covariance recursion halves cold-start time (282 s vs 474 s) and gives GPS-04 a real convergence signal. All of it is upstream, none of it ours. [`bug-GPS-uart-overflow-20260901.md`](bug-GPS-uart-overflow-20260901.md).

**Intake 2026-08-31 (operator list of 14 points), filed in §3.8p:** T-Deck map pan (`TD-07`), two presentation-timeline items (`PRES-01` Meshtastic/MeshCore + ISM footnote, `PRES-02` WSPR/WSJT-X/JS8Call), the character-set filter for message text and APRS free text (`CHR-01`, `CHR-02`), JSON validity in everything the node builds (`JSN-01`), `PM-01` (`NoPMOther` — the leak is EXTUDP, not BLE), the APRS-to-client research paper (`APRS-01`), four documentation deliverables (`DOC-01` main-loop stall page, `DOC-02` `--help` renewal, `DOC-03` CONF endianness/compatibility, `DOC-04` `config.json` register reference), `NTP-01` (cadence + report + `--ntpsync` + bench regression) and `E22-01` (frame integrity under supply spikes). **Three intake premises were corrected by the scouting** — foreign DMs are already blocked on BLE/web, TM-39 is a closed test item (the defect is `CONF-01`), and no low-voltage TX inhibit exists anywhere in the tree. Side findings: `WEB-04`, `TD-08`, `CTY-01`.

**Operator report 2026-09-03 (T-Deck Plus on 4.35s), filed in §3.8w:** "Hab ich am Deck selber grad ned oder nimmer. Im webinterface jedoch schon" — the send/delivery marker on direct messages. Verified: the T-Deck message view has never had one, in this fork or upstream back to 4.34y. The per-message state (`own_msg_id[i][4]`: not heard / heard / ACK) has exactly one consumer, the web UI. Filed as `TD-11`.

**Intake 2026-08-30 (fourth list), filed in §3.8l — TM-43 DONE the same night, UDP-01 stack question answered (424 B / 276 B free), and the run found + fixed `UDP-02` (ESP32 EXTUDP receive killed by one 255-byte datagram). PT-01: all eight parser findings fixed. §3.8m: `MEM-01` done (commit `861f2967`), `MEM-02` parked.** Original intake text: `UDP-01` — second-hand report that `--extudp on` kills a RAK4631; unreproduced, and the two defects with exactly this symptom (`N-22` stack overflow, `N-23` brick trap) are fixed both here **and** in `upstream/dev`, so the section lists what to ask the reporter before touching code. `TM-43` — the regression test the operator asked for: the RAK's UDP interface driven in both directions, send and receive, with node liveness as the assertion.

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

| ID    | Type | Sev.   | Location                                                  | Item                                                                                                                                                                                                                                | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ----- | ---- | ------ | --------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| HL-01 | GAP  | High   | `t-deck/event_functions.cpp:277`; `udp_functions.cpp:545` | `node_wifion` is assigned only in the `btn_wifi` GUI handler, and the gate that reads it is `#if`-guarded T-Deck-only. No `--wifi on/off`, so a serially provisioned **T-Deck** can never join a WLAN; other boards are unaffected. | **FIXED 2026-08-30 (Wave W)** — `--wifi on/off` sets `node_wifion`, saves, and starts/stops WLAN on the T-Deck (other boards answer `[WIFI];wifion;n/a`)                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| HL-02 | BUG  | Medium | `esp32/esp32_flash.h:239` vs `esp32/esp32_flash.cpp:204`  | Struct default is `node_wifion = true`, but the load uses `preferences.getBool("node_wifion", false)`. On virgin NVS the load default wins → every freshly flashed node boots with WLAN off.                                        | **FIXED 2026-08-30 (Wave W)** — `preferences.getBool("node_wifion", true)`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| HL-03 | GAP  | Low    | `t-deck/event_functions.cpp` (`btn_soundon`)              | `audio_set_mute()` has no serial equivalent.                                                                                                                                                                                        | **DONE 2026-08-30.** `--mute on/off` already existed; the real gap was the other way round -- `btn_soundon` called `audio_set_mute()` and then `save_settings()` **without touching** `meshcom_settings.node_mute`, so the GUI wrote the OLD value to flash and the toggle did not survive a reset, while `--mute` and the button disagreed about the state. The button now goes through `commandAction("--mute on/off")` like every other switch on that page (checked == sound on == not muted). New `--persiststat` prints all four flags in one line, so the GUI state is readable over serial at all. |
| HL-04 | GAP  | Low    | `t-deck/event_functions.cpp` (`btn_persist_*`)            | `node_persist_to_flash`, `node_persist_to_sd`, `node_immediate_save` are GUI-only.                                                                                                                                                  | **DONE 2026-08-30.** Three commands added in `command_functions.cpp`: `--persistflash on/off`, `--persistsd on/off` and `--immediatesave on/off`, each writing the settings field and calling `save_settings()`, with `[PERSIST];flash                                                                                                                                                                                                                                                                                                                                                                     | sd  | immediate;<0/1>`as the marker.`--persistsd`also calls`loadPosPersistence()`exactly as the GUI switch does -- without it the node keeps working from the old store until the next reset.`--persiststat` prints all four flags (flash, sd, immediate, mute) in one line. Help text extended. |

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
**Intake 2026-08-30 (operator list):** TM-38 (AP-reboot recovery test), TM-39 (country servers),
TM-40 (OTA in the regression), TM-41 (T-Deck colour/geometry display test), TM-42 (group `TEST`)
added below. The configuration-surface and server-data items from the same list are in §3.8h
and §3.8i.
**Intake 2026-08-30 (fourth list):** TM-43 (RAK4631 EXTUDP send/receive regression test) is
filed in §3.8l together with `UDP-01`, the report that prompted it.

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

| ID    | Board(s)                          | Type | Sev.   | Location                                                | Item                                                                                                                                                                                                                                                               | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ----- | --------------------------------- | ---- | ------ | ------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| TM-01 | T-Deck                            | BUG  | High   | `esp32_audio.cpp:289-477`, `lv_obj_functions.cpp:4045`  | `play_cw()` blocks `loopTask` ~1.1 s per message. Move tone generation off the loop task (queue + audio task) or make `playTone()` non-blocking; harness `audio_stall` scenario must drop from 1.10 s to <100 ms.                                                  | **FIXED `49c482e1`** — audio task + queue; audio_stall 1 552 -> 23 ms                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| TM-02 | T-Deck                            | BUG  | High   | `esp32_audio.cpp:139-144`                               | `play_function` created at priority 50 (clamped to 24, above everything). Set to a sane priority (3); handover C1.                                                                                                                                                 | **FIXED `49c482e1`** — prio 3                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| TM-03 | T-Deck                            | BUG  | High   | `esp32_audio.cpp:102-160`                               | `audioSemaphore` released before playback starts (C2); second message inside one sound frees decoder buffers under the reader. Hold until the task ends.                                                                                                           | **FIXED `49c482e1`** — sequential task, semaphore gone                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| TM-04 | T-Deck                            | PERF | Medium | `esp32_audio.cpp:114,203`                               | `SD.exists()` on SPI2 per message. Resolve tone file once at boot; removes the SD access from the message path (RESUME §4.6).                                                                                                                                      | **FIXED `49c482e1`** — lookup runs in the audio task under the bus mutex                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| TM-05 | T-Deck                            | BUG  | High   | `tdeck_main.cpp:52`, SD/LoRa/audio call sites           | SPI2 has no arbitration: SD (map compose, tone streaming), SX1262 and TFT interleave freely. Either one bus mutex with the audio/SD/LoRa paths taking it, or SPI transactions per user; NOP mitigation stays as belt-and-braces until then.                        | **resolved by analysis** — every SPI2 user except audio runs on loopTask (TFT flush, SD map compose from LVGL callbacks, RadioLib from OnRxDone/loop; DIO1 ISRs only set flags; web/console tasks touch no SPI); the audio task is the only cross-task user and holds the bus mutex since `49c482e1`. Arbitration is complete by construction; what remains is the same-task register state after SD access (TM-07, NOP re-arm).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| TM-06 | T-Deck                            | TEST | High   | `src/test_inject.*`, `command_functions.cpp:4509`       | LoRa harness: `--injectmsg` bypasses the radio. Need (a) raw-frame RX injection through `checkRX`/`decodeAPRS`, (b) `--loratx <n> <ms>` burst, (c) harness scenario correlating `[FLUSH]` CRC with LoRa SPI activity, (d) a second node (`DK5EN-98`) as RF source. | **(a)+(b) IMPLEMENTED 2026-08-31** — `--injectraw <hex>` stages a frame that `test_inject_service()` drains by recursing into `OnRxDone()` (identical decode/dedup/mheard/relay path; refused on RAK4631, see test_inject.cpp), `--loratx <n> <ms>` queues capped bursts via the normal TX ring; serviced from `OnRxDone()` exits plus radio-idle `loop()` (esp32_main.cpp). (c) shipped as harness scenario `flush_lora_correlation`. **Bench-proven 2026-08-31 on DK5EN-14**: `--injectraw` with a gwflood fixture frame → `[INJ];raw;len;128;res;58` (real-path decode incl. FCS/terminator strip); `--loratx` exercised by the correlation run (15 activity flushes). Caveat: command lines > ~256 chars must be written in paced chunks — the S3 USB-JTAG RX buffer drops the tail while LVGL runs. (d) unchanged operational option.                                                                                                                                                                                                                                                                                                                                               |
| TM-07 | T-Deck                            | TEST | Medium | `tdeck_main.cpp:469`, `instrument.*`                    | SPI transaction tracing: log which bus user (TFT/SD/LoRa) ran between two flushes and the SPI2 `user/ctrl/clock` registers; identifies the clobbered register (RESUME open item) so the NOP can be replaced by a proper re-arm.                                    | **IMPLEMENTED 2026-08-31** — `--spitrace on/off`: per-flush `[SPITRACE];flush;<seq>;users;T,S,L;user/ctrl/clock;chg;<changed regs>` (post-transfer GPSPI2 snapshot in tdeck_debug.cpp, S/L are edge-count proxies polled from the ~30 ms indev tick — bursts between polls undercount). Side find: `tdeck_sdmap.cpp` SD reads never take the bus mutex (audio does since 49c482e1). **Register NAMED 2026-08-31**: wave-2 refinement added a pre-transfer snapshot (`[SPITRACE];clobber`, taken before the flushfix NOP so the foreign state is visible) plus a direct SD counter in `tdeck_sdmap.cpp` (the CS-edge poll misses bursts while map compose blocks the LVGL tick). Map-zoom run on DK5EN-14: the clobbered register is **GPSPI2.clock, exclusively** — TFT runs `00001001`, SD leaves `00243002` / `00041001` behind; `user`/`ctrl` never change; LoRa activity changes nothing (574-flush correlation run, 0 chg). The NOP can be replaced by a clock-register re-arm before `pushColors` (separate decision).                                                                                                                                                             |
| TM-08 | T-Deck                            | PERF | Medium | `lv_obj_functions.cpp` (`update_header_batt_indicator`) | Header labels rewritten every 500 ms unconditionally; only `lv_label_set_text` on change (RESUME §4.3).                                                                                                                                                            | **FIXED** — header labels/colours written only on change; idle invalidations 36.9/s -> 7.0/s (harness idle, 30 s)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| TM-09 | Heltec V3/V4, Stick V3, T-Beam V3 | PERF | High   | `loop_functions.cpp:364-374`                            | OLED on `_1_SW_I2C`: switch to `_F_HW_I2C` on a hardware `Wire` instance (V2/RAK already do), set `setBusClock(400000)`. Expected ~10x faster frame; measure with `[INSTR-FLUSH]` before/after.                                                                    | **FIXED** — Heltec V3/V4, Stick V3: `U8G2_*_F_2ND_HW_I2C` on `Wire1` (SDA 17/SCL 18, `Wire1.setPins()` before `begin()`, 400 kHz, `-D U8X8_HAVE_2ND_HW_I2C=1` in the variant because arduino-esp32 never defines `WIRE_INTERFACES_COUNT`). Measured DK5EN-93: frame push 579 ms -> 34.5 ms, loop max 645 -> 39 ms. T-Beam was already on `Wire` HW I2C (37.8 ms).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| TM-10 | Heltec V3, T-Beam                 | PERF | Medium | `loop_functions.cpp:1050-1110`, `esp32_main.cpp:3003`   | Full 8-page redraw on every message and every 5 s tick; add a dirty flag and skip the push when the frame is unchanged (compare buffer hash). Partial update (`updateDisplayArea`) only if TM-09 is not enough.                                                    | **FIXED 2026-08-29** — `oledFrameUnchanged()`: in full-buffer mode the frame is drawn into RAM, CRC32 compared with the last pushed frame, identical frames are not sent (`[OLED];skip`, `skipped` in `--oledstat`). Harness `dirty` scenario: 15 s idle pushes nothing on either board; two `--display on` back-to-back -> second reported as skip (Heltec V3 + T-Beam). Note: the head page carries a clock, so frames one second apart differ legitimately; the win is the duplicate push, not the tick                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| TM-11 | all ESP32                         | BUG  | High   | `udp_functions.cpp:529-747`, `esp32_main.cpp:1642-1752` | TD-01 decisive experiment: boot with BLE advertising disabled (or started after `startNetwork()`), 10 power cycles each way. If it flips, fix ordering / coexistence; else test BSSID-pinned `WiFi.begin()` without pin and PMF.                                   | **measured 2026-08-29** on DK5EN-14, 12 boots per arm, same hour: A baseline 4/12 first joins, B BLE advertising deferred until ready 3/12 (BLE hypothesis refuted), C join by SSID only (no channel/BSSID pin) 8/12. New `[WIFI];event;disconnected;reason;N` log: every failure is reason 2 = AUTH_EXPIRE (AP does not finish authentication in time; the driver retry then often succeeds), once 202 AUTH_FAIL. Next: 24-boot confirmation of C, then ship SSID-only join + patient retry instead of the 10-poll give-up. Experiment flags `BENCH_BLE_ADV_LATE`, `BENCH_WIFI_NO_BSSID` (inert by default). **CLOSED 2026-08-30.** Root cause was WPA3-SAE plus the BSSID/channel pin (TM-34), fixed in Wave W. Confirmation run on the current build (`bootloop.py`, 24 boots, `ORBI63` WPA2/WPA3, `tools/bench/runs/bootloop_TD01_close_20260830_20260830-134157/`): **24/24 first joins, `got_ip` median 10 394 ms / max 10 437, ready median 10 949 ms, 0 disconnects, 0 connection-error bursts, 0 `[WIFI];stall` lines.** Chain: 4/12 (TM-11 baseline) -> 0/24 at 55.8 s (arm A0, pin + SAE) -> 24/24 at 14.1 s (Wave W) -> 24/24 at 10.4 s (after TM-16). TD-01 closes with it. |
| TM-12 | RAK4631                           | TEST | Medium | `nrf52_main.cpp`                                        | No loop-period instrumentation on nRF52 (`INSTRUMENT_ENABLED=0`). Port `[INSTR-LOOP]` so the W5100S/SX1262 SPI sharing (`bSPI_ETH_Active`) can be measured, not assumed.                                                                                           | **FIXED** — `INSTRUMENT_ENABLED=1` on nRF52, heap via `mallinfo()`/`dbgHeapTotal()`, `INSTR_LOOPTICK()` in `nrf52loop()`. DK5EN-90 baseline: loop avg 99.7 ms / max 104 ms (the nRF loop is paced ~100 ms), heap 111 832 free.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| TM-13 | all                               | TEST | Medium | `src/instrument.*`, `tools/bench/`                      | Per-subsystem stall attribution: wrap SD, LoRa, audio, display, GPS entry points with an `INSTR_SECTION(name)` and report max per section; makes every later fix measurable on all boards.                                                                         | **DONE 2026-08-29** — `INSTR_SECTION(name)` (RAII scope in `instrument.h`), per-name n/avg/max as `[INSTR-SECT]` in `--instr`, and every loop gap > 250 ms prints `[INSTR-LOOP];gap;ms;N;in;<longest section>;section_ms;..;sections_ms;..` (sections_ms << gap = the blocker is outside every section). 9 sections on the ESP32 loop (lora_rx, gps, ble_cmd, udp, lvgl, display_tick, display_rx, wifi_ping, wifi_connect), 33 on the nRF52 loop. First catch: the RAK's periodic 1.6–3.3 s loop stall is `neth.getUDP()` (eth_udp) and `sendHey()` 0.7–1.4 s (TM-35)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| TM-14 | T-Deck                            | TEST | Low    | `gps_functions.cpp:862`                                 | GPS control experiment (`--gps on/off`, same injected load, compare `[INSTR-LOOP]` tails) — closes the GPS question with data rather than by reading.                                                                                                              | **scenario built 2026-08-31** (`tdeck_harness.py --scenario gps_experiment`, interleaved inject+instr sub-windows, p50/p90/p99/max + tail counts per phase). **MEASURED 2026-08-31 on DK5EN-14** (2×120 s, load 5): GPS ON p50/p99 102/119 ms vs GPS OFF 101/677 ms — GPS is NOT the loop-tail culprit; the one 677 ms outlier sat in the GPS-OFF window and is flagged by the scenario itself as noise. n≈5 samples per phase — rerun longer if a tighter bound is ever needed (`runs/wave2_tdeck_gps_20260831.json`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| TM-15 | T-Deck                            | PERF | Low    | `esp32_main.cpp` boot messages                          | 2 s busy-wait per boot message (8 = 16 s); replace with non-blocking sequencing (RESUME §4.7).                                                                                                                                                                     | **FIXED** — 100 ms LVGL pump per boot message instead of 2 s; CLIENT STARTED 17.8 -> 4.6 s                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |

| TM-16 | T-Deck | PERF | Medium | `esp32_main.cpp` setup, `udp_functions.cpp:529-747` | Boot to `[BOOT];ready` takes 29 s (reset -> network phase settled; `CLIENT STARTED` at ~18 s). Operator sees the start tone, then header icons still blinking. Profile the setup path (SD mount, LVGL build, BLE init, WiFi scan+join) and cut the waits. | partly — boot messages (TM-15), WiFi bring-up (TM-20) and the ready marker fixed (`Serial.printf`, not `printfdeb`: outside `--debug csv` the semicolons were stripped; checks the global `hasIPaddress`). Ready: T-Deck 11 s, Heltec 9 s, T-Beam 9.5 s with WiFi joined. **DONE 2026-08-30** — the two remaining costs measured on DK5EN-14 and cut. (1) `SetupUBLOX()` ended with `WaitPause()` + `sendUBX_MON_VER()` + `readUBXbin()`; the version string is read only for a `[GPS_VER]` debug line (`ver` is file-local, no other reader), `readUBXbin()` always runs into its 500 ms timeout because it only retriggers and never returns early, and `WaitPause()` waits up to 1000 ms for the next character block. Now gated on `iGPSDEBUG >= 2`: SetupUBLOX 1 933 -> 899 ms, GPS init total 3 503 -> 2 001 ms, no change to the module configuration itself. (2) `startNetwork()` ran a full `WiFi.scanNetworks()` before every `begin()`, costing 3-5 s -- but since Wave W the driver picks the AP itself (`WiFi.begin(ssid, pwd, 0, NULL, false)`) and `wifiLogScan()` only prints and discards. The scan is now skipped on the **first** bring-up after reset (`[WIFI];scan;skipped;first_bringup`) and kept on every later one (radio restart, watchdog -- i.e. after something went wrong), so the field diagnosis survives exactly where it is needed; verified with `--wifi off`/`--wifi on`, which prints the AP list again. `--wifistat` now reports `bringups;N`. **Measured, 3 boots:** T-Deck ready median **10.76 s** (10 587 / 10 758 / 10 805), `got_ip` 10.36 s -- against Wave W's 24-boot baseline of `got_ip` median 14.1 s and a 14 873 ms ready. Heltec ready 8.2 s, T-Beam 7.5 s, RAK boot/info PASS; GPS still detects UBLOX and gets a fix on all of them. |
| TM-17 | all ESP32 | BUG | Low | `esp32_main.cpp:3745-3776`, `udp_functions.cpp:737` | `bAllStarted` never becomes true when WiFi joins inside `startNetwork()` (iWlanWait=0 and IP set -> the loop branch that sets it is skipped). Only effect found: the `bHeyFirst && bAllStarted` early HEY/telemetry never fires on a clean boot. Ready marker deliberately does not rely on it. | **DONE 2026-08-30 (Wave W)** — `bAllStarted` is set wherever `wifiHarvestGotIp()` applies a driver `got_ip` (`esp32_main.cpp` loop), so a clean boot join sets it too |
| TM-18 | T-Deck | PERF | Medium | `tdeck_main.cpp` `mouse_read()` | Trackball "not smooth": GPIO levels are polled every 10 ms (`LV_INDEV_DEF_READ_PERIOD`) and compared to the last level, 10 px per edge. Edges faster than the poll collapse (odd count -> one step, even count -> none). Injected steps are perfect (input scenario: 40/40, p95 30 ms), so the loss is at the edge sampling. Measure with an ISR edge counter vs consumed events under a real roll, then count edges in the ISR and consume them in `mouse_read()`. | **FIXED** — ISR edge counting, consumed in `mouse_read()`; measured on DK5EN-14: old level compare lost ~75 % of edges on fast rolls (186 -> 40, 224 -> 57), edge mode 0 lost, pending 0, repaint p50 8 ms on tab 0 and map. `--balledge off` restores the old path for A/B. |
| TM-20 | all ESP32 | BUG | High | `udp_functions.cpp` `startNetwork()`, `esp32_main.cpp` retry path | **FIXED** — WiFi bring-up froze loopTask ~7 s (delay 1000+200+500, sync `scanNetworks()`) plus `delay(1500)` on the boot retry, and again on every 5-minute retry while unconnected (LoRa RX unserviced on any ESP32 gateway without AP). Now async scan, no delays; measured on DK5EN-14: no gap > 0.7 s in the boot log, loop max 26 ms. Trackball edges banked during a stall are discarded, not replayed. **Operator finding 2026-08-29 (late): with every boot delay removed the station no longer hears the beacons of all nearby APs before it picks one, and can associate with the weakest BSSID of the mesh.** Feels good on the bench, but shipped as is it would degrade every node behind a multi-AP WLAN. | FIXED on the trace branch — **stays OUT of the T-Deck PR** (decision 2026-08-29 late, reverses the evening decision). The AP-selection problem is TM-34's question (b); TM-20 goes upstream only together with a scan/selection strategy that TM-34 has settled. |
| TM-21 | all ESP32 | PERF | Low | `esp32_main.cpp` (`if(bWEBSERVER && iWlanWait == 0) startWebserver();`), `web_functions.cpp:81` | With `--debug on` and no IP (AP unreachable, after the 5-min give-up) every loop pass prints `[WEB]...no ip set` — ~125 lines/s (10 630 in 100 s on DK5EN-93). Pre-existing; print once per state change. | **FIXED** — logged once per state in `startWebserver()`, latch reset when an IP is present |
| TM-22 | T-Beam (all `_HW_I2C` OLED boards) | PERF | Medium | `loop_functions.cpp:379-380` (`#else` branch) | T-Beam is already on `Wire` hardware I2C (37.8 ms/frame, DK5EN-92, SH1106 `_F_`), but the SSD1306 variant still uses `_1_HW_I2C` page mode (8 transfers/frame) and no explicit `setBusClock(400000)`. Same treatment as TM-09; plus TM-10 dirty flag so unchanged frames are not pushed at all. | **FIXED 2026-08-29 (compile-verified only for the SSD1306 half)** — `#else` ladder SSD1306 `_1_HW_I2C` -> `_F_HW_I2C` (page mode is gone from every ESP32 HW-I2C board). DK5EN-92 is the SH1106 (`_F_` already), so the SSD1306 path has no bench node. Bus clock: the T-Beam's Wire was measured already at 400 kHz (35.8 ms/frame ~ 1 KB at 400 kHz), an explicit `setBusClock` changed nothing and was dropped |
| TM-24 | all ESP32 | BUG | High | `udp_functions.cpp` `wifiBeginFromScan()`, `checkWifiPing()` | **No roaming at all** (found 2026-08-29): the SDK is built without 802.11k/v (`CONFIG_WPA_11KV_SUPPORT` not set, no 11r), the firmware never sets `rm_enabled/btm_enabled`, never calls `setAutoReconnect()`, and pins channel+BSSID of the strongest AP seen at scan time in `WiFi.begin()`. Against a mesh WLAN (ORBI63 = router + satellite, two BSSIDs in every scan) that steers clients, the pinned station cannot follow a BSS-transition request and is deauthed/ignored: reason 2 `AUTH_EXPIRE` at join (TM-11 data), and later drops only recovered by the ping watchdog (`checkWifiPing` -> full `startNetwork()` restart). Fix: join by SSID only (driver picks and may switch BSSID), `WiFi.setAutoReconnect(true)`, drop the BSSID pin; optionally enable 11k/v via `esp_wifi_set_config` (`rm_enabled`, `btm_enabled`) once the SDK supports it. Supersedes the TD-01 "which AP" discussion. **Two corrections 2026-08-29 (TM-34). (1) §6: "never calls `setAutoReconnect()`" was read as "reconnect is off" — it is not. `WiFiSTA.cpp:116` defaults `_autoReconnect = true` and `AUTH_EXPIRE` is in the reconnectable set (`WiFiGeneric.cpp:1182`), so the driver has been retrying all along — with the pinned config, which is why it can never pick another AP. (2) §2: "optionally enable 11k/v via `esp_wifi_set_config` (`rm_enabled`, `btm_enabled`)" is a trap — do NOT do it. Those bits have live code only in `libnet80211.a` (`esp_wifi_is_rm/btm_enabled_internal`), and all it does is advertise the capability in the association request; the supplicant side (`esp_rrm_*`, `esp_wnm_*`) is defined in no library of the 4.4.7 set and the headers do not exist. Setting them makes the node claim 11k/v it cannot honour, so a steering AP starts sending BTM requests we drop — worse than staying silent. There is no build flag either: PlatformIO links prebuilt `.a` files, so `sdkconfig` records how Espressif built them and is not an input to our build. The rest of the entry (11k/v out, no 11r, pin removes the fallback) is confirmed.** | absorbed by TM-34 — **fixed 2026-08-30 (Wave W, F1/F2)**: no BSSID/channel pin, driver selects by signal on every attempt; see TM-34 |
| TM-25 | RAK4631 | PERF | Medium | `nrf52_main.cpp` setup | Boot time not profiled on the RAK (no `[BOOT];ready` marker on nRF52 yet; host-timestamped boot log needed). Part of the "boot time on all four platforms" goal. | **DONE 2026-08-29** — `[BOOT];ready;ms;N;ip;X;eth;Y` on the nRF52 (first loop pass, network phase is synchronous in setup). Profile DK5EN-90 (host clock from `--reboot`): USB back 4.6 s, `START CLIENT` 4.8 s, `CLIENT STARTED` 16.3 s (11 s inside setup before the radio), Ethernet DHCP 17.5→23.4 s, ready 23.4 s (fw 19.5 s). Note: one `--reboot` produced two boots once (RESETREAS 0x4 twice, 24 s apart) — the harness counts boots |
| TM-26 | RAK4631 | TEST | Medium | `tools/bench/` | No harness for the RAK: needs a serial scenario set (boot, `--info`, LoRa TX/RX via the other nodes, `--instr` loop/heap, W5100S gateway paths N-20) — the fourth platform of the regression goal. | **DONE 2026-08-29** — `tools/bench/rak_harness.py` (boot, info, instr, lora, mheard; `--peer-port` for the cross-node TX proof). RakSession: dtr=True, tolerates the USB re-enumeration after `--reboot`. First runs: boot/info/mheard PASS (hears DK5EN-14/-92/-93/-98), instr FAIL on loop max (see TM-35), lora: `--sendpos` gives no TX marker on the gateway build — proof moved to the peer's `--mheard` |
| TM-27 | Heltec, T-Beam, E290 | TEST | Medium | `loop_functions.cpp` `sendDisplay1306()` | Pixel-level verification for OLED/e-paper: the U8g2 frame buffer is in RAM — print a CRC32 of the buffer per push (`[OLED];crc`) so the harness can assert screen content, which the T-Deck cannot (no panel readback). | **FIXED 2026-08-29** for U8g2 boards — CRC32 of the frame buffer per push: `[OLED];frame;…;crc;<hex>;skipped;<n>`, `[OLED];skip;…`, `crc`/`skipped` in `--oledstat`. Harness `display` asserts the round trip: every off frame is the blank buffer (`0xefb5af2e` on both boards), every on frame differs. E290 still open (TM-28) |
| TM-28 | E290 Wireless Paper | TEST/PERF | Medium | WP_DISP paths, e-paper driver | Hardware arrives week of 2026-09-01: frame/refresh instrument, harness scenarios (browse loop, message pages, deep sleep), redraw cost per page. | delayed until the hardware is here (week of 2026-09-01) |
| TM-29 | all | TEST | Low | `docs/automation-runner-runbook.md` | Unattended regression runner (bench Mac, schedule, fail on any FAIL/crash marker). Manual procedure and expected outcomes are in the runbook; blockers: TM-26, TM-27, audio assertion, four-node exchange as a script. | open — low priority |
| TM-19 | T-Deck | TEST | Low | `tools/bench/tdeck_harness.py` | Harness gaps: touch injection (none), real-roll trackball capture (TM-18), audible confirmation of tones (operator only). | **touch injection IMPLEMENTED 2026-08-31** — `--touch tap <x> <y> [ms]` / `down` / `up` through the real LVGL indev path (queue in tdeck_main.cpp, drained by `touchpad_read()` before the GT911 poll, `[TOUCH];inj;...` markers) + harness scenario `touch_inject`. **Bench-proven 2026-08-31 on DK5EN-14**: 3 taps + down/up all ack'd with a flush following (`runs/wave2_tdeck_std_20260831.json`, full standard harness PASS alongside). TM-18 trackball capture and tone confirmation stay open. |
| TM-23 | T-Deck | PERF | Low | `tdeck_sdmap.cpp`, tile set on SD | Tile format / decoded-tile PSRAM cache (RGB565, QOI, palette PNG; ~95 ms lodepng per tile). **Parked by decision 2026-08-29: stability and functionality first.** Options in `tdeck-findings-20260828.md` §5. | parked |
| TM-30 | T-Deck | BUG | High | unknown — bisect first | **Upstream #1083** (dl9sec, closed _"nicht nachvollziehbar"_): T-Deck Plus touch responds only after tens of seconds once the node has been up ~10 min, while **web interface and OTA stay normal** — a loop-starvation signature that grows with uptime, not a hang. Reporter handed over a free bisect window: `v4.35p.07.26` good, `v4.35p.08.06` and `v4.35p.08.10.2` bad. Upstream landed in that window: the external-radio TCP feature (PR #1072, merged 2026-07-31), `--setlog on/off` (`c78adcc5`, mask corr `a7bf5474`), `HELTEC_V3 ADC_FACTOR`, PL country. Our `external_radio_glue.cpp` is non-blocking by construction, so the obvious suspect does not obviously apply to us — **check, do not assume.** Run: DK5EN-14, 30 min uptime with `[INSTR-LOOP]` on, watch the loop tail and the input latency; then bisect the window if it reproduces. Note none of our fixed items (TM-01…TM-05, TM-08, TM-15, TM-18, TM-20) explain a defect that grows with uptime. | **not reproduced 2026-08-30 (35 min, DK5EN-14, `tdeck_harness.py --scenario uptime`)** — 7 probes 5 min apart: input latency 27/22/22/38/36/28/17 ms (flat), loop max 0.59–0.69 s per step (a fixed per-probe cost, not growing), internal heap 104.1 → 102.6 kB, PSRAM −9 kB over 35 min with 3 injected messages, 0 gaps, no crash. #1083's window (~10 min) is covered. Whatever the reporter hit in v4.35p.08.06/.10.2 is not on this branch after TD-03/UP-02/TM-01..20; keep the scenario as the regression gate. Instrument note: `--instr` reported max_us ≈ 590 000 without a matching `[INSTR-LOOP];gap` line — check whether the max is taken inside the `--instr`/`--heap` command handling itself (printfdeb burst) before trusting sub-second maxima on the T-Deck. |
| TM-31 | all ESP32 (gateway) | BUG | High | `udp_functions.cpp` UDP→LoRa path, `dedup_functions.cpp`, TX queue | **Upstream #568** (HelmutEsterer, closed _"Ich lege das für einen späteren test zur seite"_ — never tested). Evidence recovered and analysed 2026-08-29: the attached gateway log is still live and contains the named packet. DB0SEP-12 → DJ8MEH-46 BBS listing = 7 UDP messages into a LoRa gateway in 45 s, all ~128 bytes; inter-arrival gaps 8, 7, 11, **4**, 8, 7 s. **The one packet the reporter says never reached the target (`A2659021`) is precisely the one arriving on the shortest gap.** Same size as its neighbours, so this is a timing effect, not a length effect — a gateway fed faster than it can radiate at SF11/BW250. Matches our own DG0OPK finding (TX-queue latency, two proven dedup overflows). The log shows only the ingress side, so it does not by itself prove the drop — but it gives an exact bench experiment: feed a gateway N equal-sized UDP messages at decreasing inter-arrival and count LoRa TX vs ingress. Log: `user-attachments/files/21353203`. | **instrument built 2026-08-29, experiment blocked at the LAN** — `tools/bench/experiments/gwflood.py`: hosts the mock MeshCom server (`tools/mock`), points the gateway at it with the new fork-only `--srvip <ip>` hook (RAM only; re-runs `startMeshComUDP()` when `WL_CONNECTED`), sends f001 frames with fresh msg_ids + recomputed FCS at decreasing inter-arrival, counts gateway `TX-LoRa` and observer `RX-LoRa2` per msg_id and the ingress→TX latency. Status: the Heltec registers (`[KEEP]` every 30 s to the override address, `--loradebug on` on both nodes) but **no datagram reaches any UDP socket on the Mac** (raw listener, venv python and firewall-allowed uv python alike; ICMP Mac→Heltec fine). Next: `sudo tcpdump -ni en0 udp port 1990` on the Mac, or run the mock on mcapp.local; if the Orbi isolates 2.4 GHz clients, use the RAK (Ethernet) as the gateway under test. Side catch while doing this: the F3 blind window live — Heltec first join failed, driver reconnected at 53 s, nobody harvested it until the hook did. **UNBLOCKED 2026-08-30 -- the LAN was never the problem.** The old verdict came from a blind instrument: the whole ESP32 UDP receive path logs only through `DEBUG_MSG()` (compiled away, `DO_DEBUG 0` in `debugconf.h`) and `printfdeb()` behind `--debug on`, so "no datagram arrives" could not be told apart from "nothing is logged". Added a fork-only UDP instrument (`--udplog on/off` -> `[UDP];rx;ip;..;port;..;len;..;head;..` and `[UDP];tx;..;ok;..` per datagram, `--udpstat` -> `[UDPSTAT];bind;rx;tx;tx_fail;last..`, counters always kept). Re-measured both directions on the bench LAN: Mac -> Heltec 3/3 (`[UDP];rx;ip;192.168.68.58`), Heltec -> Mac 6/6 with `--srvip 192.168.68.58` (KEEP `4b454550` + DATA `44415441` at the host listener). No sudo tcpdump, no Orbi change needed. `gwflood.py` now opens a node that does not reset on port open (T-Beam v1.2: falls back to a forced `--reboot`) and reports ingress / `[UDP];rx` seen / `RING_WRITE src=udp_rx` queued / LoRa TX / observer RX, so a loss can be attributed to a stage. **EXPERIMENT RUN + DEFECT FOUND AND FIXED 2026-08-30.** First measurement with the new instrument: 30/30 GATE frames reach the socket, **0 queued, 0 radiated** at every inter-arrival. Cause is not timing but a self-dedup in `getMeshComUDPpacket()`: the `is_new_packet()` gate was evaluated _after_ the `msg_type_b == 0x21` branch had already inserted the msg_id via `addLoraRxBuffer()`, so every UDP position frame deduplicated against the entry it had just written itself (`RX_DEDUP_ADD slot N`, 17 ms later `RX_DEDUP_DUP slot N`) -- an ESP32 gateway never relayed a UDP position frame to LoRa. The nRF52 path never had the early insert. Fix: read the gate before that branch (`is_new_packet()` is side-effect free) and skip the now-redundant second insert on the queue path, so the ring insert rate is unchanged. After the fix 30/30 queue and the T-Beam observer hears them off the air. **#568 then answered** (`gwflood_fixed_settle300_20260830.json`, 300 s settle): nothing is lost at ingress -- the radio drains ~1 frame / 20 s under bench channel load, ingress->air median rises 78 s (8 s gaps) -> 310 s (1 s gaps), and at 1 s and below the **20-slot TX ring saturates** (`queued=19/20`) and the firmware discards the arriving frame: 6x `RING_DROP_NEW ... (queue full, no lower prio to evict)` plus 1x `RING_DROP_PRIO` evicting the node's own HEY. A gateway fed faster than it can radiate delays by minutes, then drops at the TX ring, and starves its own traffic -- consistent with #568 (the lost packet was the one on the shortest gap) and with the DG0OPK TX-queue finding. **Text case measured 2026-08-30** (`gwflood_mixed_20260830.json`, 15 text + 15 position frames alternating, gaps 8/4/2/1/0.5 s, 300 s settle): **text 15/15 radiated, position 9/15**. The ring-drop lines give the reason -- 5x `RING_DROP_PRIO ... prio=4 type=21 ... replaced_by_prio=3` (arriving text evicts queued positions), 2x the node's own HEY evicted by a position, 2x HEY tail-dropped. The priority ladder therefore works under a real flood: text survives, positions are sacrificed, the node's own background traffic goes first. `--assert-relay` passed. Nuance: a UDP-relayed text is enqueued with status `0xFF` = `RING_STATUS_DONE`, so `getMessagePriority()` classes it as a _relay_ (`MSG_PRIO_NORMAL` 3), not as the path-based broadcast (`MSG_PRIO_HIGH` 2) -- still above position (4), but whoever changes that status byte moves the priority of all UDP traffic. Injector now has `--frame pos|text|mixed`, `--text-bytes` (default 128 = #568's size), `--assert-relay` and `--dump-frames`. Regression tests: `test_txring_flood` (8, queue-full policy incl. tail-/head-drop and `MAX_RING == 20` pinned) and `test_gwflood_frames` (6, real `decodeAPRS()` over the checked-in injector fixture). **Remaining, as policy questions rather than defects:** tail-drop vs head-drop on a full queue, and whether 20 slots (~6 min of buffered airtime at bench drain rate) is right for a gateway — both pinned by `test_txring_flood`, so either change is a reviewable diff. See also TM-37 (the sender is never told when its own message is the one dropped).|
| TM-32 | all | BUG | Medium | `esp32_flash.cpp:56-60`, `nrf52_flash.cpp` load path | **Extends the `N-12` fix.** We already validate struct _integrity_ on load (markers + `sizeof` check → `flash_reset()` to defaults). We do **not** validate field _plausibility_: `node_power`, `node_freq`, `node_bw`, `node_sf`, `node_cr` are read straight out of NVS and only the `-20` sentinel triggers a default. A struct that passes the markers but carries an out-of-range radio value goes unchecked into `radio.setOutputPower()`. That is the gap behind upstream **#661** (m-ugo, RAK4631: brown-out corrupts settings, firmware crashes, only recovery is formatting the flash — upstream answer was `--cleanflash`, which needs a working serial console and so does not help a bricked remote node) and **#57** (dl7ata: `RF_POWER: 1` → no LAN, no serial, no BLE; patched narrowly by clamping TX power ≥2 dBm in #140, never by validating the load path). Fix: range-check the radio parameters at load, fall back to the board default and log it. | **FIXED 2026-08-29** — `src/settings_sanitize.{h,cpp}` (pure C++, native `test_settings_sanitize` 10/10): power outside [TX_POWER_MIN, MAX] → sentinel -20, freq outside 400–960 MHz (Hz on nRF52) / NaN → 0, bw not 125/250/500 (index 0..2 on nRF52) → 0, sf not 6..12 → 0, cr not 5..8 (1..4 nRF52) → 0, country ≥ max_country → 0; nRF52 also NUL-terminates every char field of the raw-read struct. Called after the load on both platforms, corrections logged `[FLASH]...sanitized <field>: old -> new` and saved once. Bench: `--flashpoke sf 99` / `power 99` / `cr 1` on DK5EN-93, next boot logged three corrections, radio came up SF11 / CR 4/6 / 22 dBm |
| TM-33 | T-Deck | TEST | Medium | `tdeck_main.cpp:189`, `command_functions.cpp:873-908`, `lv_obj_functions.cpp:4015-4023` | Regression tests against three upstream T-Deck reports that were closed without a fix. **All three verified still present on this branch 2026-08-29 by reading — none is fixed by our work so far:** (a) **#64** touch fails at boot (`touch: failed`); `touch.begin(Wire)` is a single attempt with no reset pin (`setPins(-1, …)`) and no retry — one failure disables touch for the whole session. Reporter's hypothesis: `TOUCH_INT` GPIO16 is pulled differently on T-Deck vs T-Deck Plus, so GT911 address selection races at reset. Maintainer dismissed it, then conceded he sees it too on a weak battery. (b) **#690** `--display on/off` over serial is inert on T-Deck: the command sets `bDisplayOff`/`bDisplayIsOff` and calls `sendDisplayHead()` — the U8g2 path; `grep bDisplayOff src/t-deck/` returns **nothing**, the TFT backlight is never touched. (c) **#690** `SET > WIFI` shows OFF while WiFi is connected: the switch reflects the stored intent flag `node_wifion`, not the association state — so a node whose WiFi came up because gateway/webserver/netconsole forced it (cf. #1059) shows a wrong switch. Plus **#267**: T-Deck battery value reported frozen on the dashboard while a T-Beam beside it was fine — bench check on DK5EN-14 against the beacon, not the local header. | **(a) FIXED 2026-08-29** — `touch.begin(Wire)` retried up to 5x, 100 ms apart (`TDECK_TOUCH_INIT_TRIES`), `touch_tries` on `[BOOT];init`, `[INIT]...touch: … after N attempts` when it took more than one. Bench: DK5EN-14 succeeds on attempt 1 — the #64 failure (weak battery, GT911 not answering yet) is not reproducible here, so the retry is verified for the good path only. **(b) FIXED 2026-08-29** — `--display on/off` now calls `tft_on()`/`tft_off()` on the T-Deck (was the U8g2 path only, a no-op there); harness `displaycmd`: before-fix 0/4, fixed 4/4 (`[TFT];off/on` + `--tft state` sleeping 1/0). Keys/touch keep waking the panel; the persisted flag is not applied at boot. (c) **closed by analysis 2026-08-30**: on the T-Deck `startNetwork()` refuses when `node_wifion=false`, so the switch (intent) cannot disagree with a live association; with HL-01/HL-02 fixed the intent is serial-settable and defaults to on. No GUI change. |
| TM-34 | all ESP32 | RESEARCH | High | `udp_functions.cpp` `startNetwork()`, `wifiBeginFromScan()`, `doWiFiConnect()`, `checkWifiPing()`, `wifiEventLog()`; arduino-esp32 / ESP-IDF WiFi driver and `sdkconfig` of the framework build | **WiFi investigation — a separate research track, not part of the T-Deck PR (opened 2026-08-29 late).** Trigger: TM-20 removed every boot delay and the node now associates before it has heard all beacons — sometimes with the weakest AP (see TM-20). TM-24 (no roaming, BSSID pinned) and TD-01/TM-11 (`AUTH_EXPIRE` on first join) are the same problem seen from other sides. Questions to answer, each with evidence from the driver source/sdkconfig and a bench measurement, before any of it is coded: (a) **Driver capabilities** — which arduino-esp32 / ESP-IDF version is in the build, what the WiFi stack supports and what is compiled out (`CONFIG_WPA_11KV_SUPPORT`, 11r, `rm_enabled`/`btm_enabled`, `WIFI_FAST_SCAN` vs `WIFI_ALL_CHANNEL_SCAN`, `sort_method`, `threshold.rssi`, `scan_method`, PMF, band selection on the C3/S3 targets). (b) **Scan and selection** — options for scanning long enough to hear every AP of the SSID (passive vs active scan, per-channel dwell `scan_time`, full-channel scan) and then joining the best one without re-introducing a loop stall (async scan is already in place; what is missing is the wait/selection policy, not the blocking). (c) **Stall log** — the WiFi stack can stall the whole node, blocking even LoRa reception: add log output that names the stall (which call, how long, from which task — `[WIFI];stall;…`, loop-gap instrument correlated with `wifiEventLog()` events) so the field can report it and we can reproduce it. (d) **SSID-only association** — can we drop the BSSID+channel pin in `WiFi.begin()` and let the driver choose (the `BENCH_WIFI_NO_BSSID` experiment: 8/12 vs 4/12 first joins on DK5EN-14); what changes in AP selection when we do. (e) **Re-connecting** — `WiFi.setAutoReconnect()`, driver-side reconnect vs our `checkWifiPing()` -> full `startNetwork()` restart; who owns reconnect, and how the two must not fight. (f) **Roaming** — what the station can do without 11k/v/r (RSSI-threshold rescan, `esp_wifi_scan` while connected) and what needs the SDK options; behaviour against a steering mesh (ORBI63 router + satellite on the bench). (g) **Band and AP steering** — how the node reacts to BSS-transition requests and to APs that deauth/ignore a client to push it elsewhere; 2.4 GHz-only hardware against dual-band steering; whether pinning the mesh's 2.4 GHz BSSID is ever the right call. Deliverable: a findings doc (`docs/wifi-findings-<date>.md`) with one recommendation per question, then a fix plan with its own bench protocol (24-boot runs per arm, `scratchpad/bootloop.py` pattern) and its own PR — separate from the T-Deck PR. Absorbs TM-24 and the TD-01/TM-11 confirmation run. | **desk half done 2026-08-29** — [`wifi-findings-20260829.md`](wifi-findings-20260829.md) answers (a)-(g) from the driver source, `sdkconfig` and the prebuilt libraries, with a fix plan (F1-F9, §9) and a bench protocol (arms A0-A5, §10). Runner written: `tools/bench/experiments/bootloop.py` (parser verified against a real boot log). **Open: no bench arm has been run** — DK5EN-14 was occupied by the TD-03 harness. Headline findings: (1) our scan+best-RSSI+pin re-implements `WIFI_ALL_CHANNEL_SCAN`+sort-by-signal, worse and only once; (2) `setAutoReconnect` was already on — three restart owners fight instead; (3) the biggest remaining `loopTask` blocker is `hostByName()` at up to 31 s, not WiFi bring-up; (4) TM-20 should not be reverted, it ships with the policy fix. Next: run arm A5 (WPA2-only test SSID) first, it is the cheapest and decides SAE-vs-steering. **A5 run 2026-08-29 (bootloop.py, DTR/RTS fix): 24/24 first joins on the WPA2-only `ORBI63_Guest`, got_ip median 9.6 s / max 10.1 s, 0 disconnects** — vs 4/12 on the WPA2/WPA3 `ORBI63` (TM-11 A0). Same Orbi, same steering: the first-join `AUTH_EXPIRE` is the WPA3-SAE transition path, not band steering. F1–F4 must therefore include the security mode: force WPA2-PSK on WPA2/WPA3 APs (`setMinSecurity`/authmode threshold, or disable SAE) and re-measure A0 on `ORBI63` with the DTR/RTS-fixed runner (needs the ORBI63 password to switch DK5EN-14 back). **A0 run 2026-08-30 (`bootloop_A0_20260830-084907`): 0/24 first joins on WPA2/WPA3 `ORBI63`, got_ip median 55.8 s / max 56.5 s, 240 disconnects (216× `2:AUTH_EXPIRE`, 24× `202:AUTH_FAIL`); every boot pins the router BSSID `5A:AF:97:2E:2B:8B` (-68…-75 dBm, satellite `46:…:86` at -84…-94, both CHAN 3), fails 9 attempts incl. the firmware's radio-reset retry, and the driver's auto-reconnect lands the 10th at ~55.4 s — same radio as A5's guest VAP `5A:…:8C` at the same RSSI, so the 0/24 vs 24/24 gap is the security mode alone.** **Wave W shipped 2026-08-30 (F1–F7 + SAE policy + instrumentation, `udp_functions.cpp`/`esp32_main.cpp`/`command_functions.cpp`): driver-owned selection (`ALL_CHANNEL_SCAN` + sort-by-signal, `persistent(false)`, `setAutoReconnect(true)`), SSID-only `begin()`, our scan is log-only, `BENCH_WIFI_NO_BSSID` deleted; SAE: `esp_wifi_disable_pmf_config(WIFI_IF_STA)` after `begin(…, connect=false)` — SAE needs PMF, without it the station authenticates WPA2-PSK on a WPA2/WPA3 AP (`WIFI_SAE_POLICY` 0 driver default / 1 PMF off = default / 2 H2E+HnP; `[WIFI];assoc` logs the negotiated `pmf`/`wpa3` via `esp_wifi_sta_pmf_enabled()`/`esp_wifi_sta_prof_is_wpa3_internal()` from `libnet80211.a`); F3 `got_ip` flag set in the event handler and harvested in the loop (`wifiHarvestGotIp()`, also after the 20-s poll window, no boot radio reset, 5-min path only when `wifiTrulyOffline()`); F4 watchdog 180 s grace -> `disconnect(false,false)+begin()` -> 360 s radio reset; F5 `[WIFI];stall;<site>` (mode, disconnect, scan, begin, dns); F6 lwIP `dns_gethostbyname` async with cache and literal short-circuit (57 ms on the bench, was up to 31 s blocking); F7 dead branch deleted; `[WIFI];link` 60-s heartbeat, `--wifistat`, `--wifidrop`. **Arm A4p1 (all fixes, PMF off) on WPA2/WPA3 `ORBI63`: T-Deck 24/24 first joins, `got_ip` median 14.2 s / max 14.7 s uptime (bring-up starts at 5.2 s: diagnostic scan ~5 s + driver scan/join ~4 s), 0 disconnects, 0 stalls; Heltec 12/12, 11.2 s; T-Beam 12/12, 10.6 s** (`tools/bench/runs/bootloop_A4p1_*`). Baseline A0 was 0/24 at 55.8 s. All ESP32 boards SNR-less: RSSI only. Soak runner `tools/bench/experiments/wifisoak.py`. **A4p0 (same code, `WIFI_SAE_POLICY=0`, SAE kept): 24/24 addressed by 19.7 s, every join SAE (`pmf;1;wpa3;1`), 0 disconnects — but exactly every second boot needs a silent second SAE attempt (+4.5 s: `got_ip` 13.6–14.7 s vs 18.2–19.7 s, alternating), which the runner scores as 12/24 (its first-attempt rule). So the A0 0/24 was the BSSID pin _and_ SAE together; unpinned SAE works, WPA2-PSK is deterministic. Policy 1 stays the default (operator may flip it; policy 0 keeps WPA3-only APs). **Found and fixed in the same wave: the first `[WIFI];link` hook called `WiFi.getMode()` every loop pass — an `esp_wifi_*` API call waits for the WiFi task and blocked `loopTask` 2.7–2.9 s while the driver scanned (plus 1.1 s during the connect scan), in every A4p1/A4p0 boot. Loop hooks now use the STA_START/STA_STOP events (`s_wifiStaUp`) and never call the driver while it may be scanning. Rule: no `esp_wifi_*`/`WiFi.getMode()` from the main loop unless connected.** **A4p1b (hook fix, policy 1, `bootloop_A4p1b_*`): 24/24, `got_ip` median 14.1 s / max 14.6 s, 0 disconnects, 0 stalls; the 1.1 s connect-scan gap is gone.** What remains is one 2.6–3.5 s loop gap per boot at ~8.5 s uptime with `section_ms` 32 (outside every section, right after the GPS/UBLOX init) — pre-existing: the A0/A5 build predates the TM-13 gap reporter and A0 shows the same single scan-poll in that window, so it is a TM-16 boot-time lead, not WiFi. Open: overnight soak, WPA3-only APs will not associate with policy 1 (documented trade). |
| TM-35 | RAK4631 (gateway) | BUG | High | `nrf52/nrf_eth.cpp` `getUDP()`, `nrf52_main.cpp` `sendHey()` | **Found by TM-13/TM-26 on 2026-08-29.** With the W5100S link up and the gateway registered, `neth.getUDP()` blocks the loop for 1.6 s about every 20 s (three 3.3 s stalls in 90 s before the sections were complete) and `sendHey()` for 0.7–1.4 s once after boot. Loop max 1.7–3.4 s vs a 100 ms paced loop. RX runs on the timer task on nRF52, but relay/TX, serial and BLE commands wait. Same family as N-20 (socket SPI paths). Measure first with `rak_harness.py --scenario instr`, then bound the W5100S socket calls. | **instrumented 2026-08-30 (Wave W parity)** — `nrf_eth.cpp`: `[ETH];stall;<site>;ms;N;task;X` (sites `udp_rx`, `udp_read`, `udp_tx`, `udp_stop`, `dhcp_begin`, `dhcp_maintain`, `link`, `hw_init`, `ntp`, threshold 50 ms), `[ETH];event;link up/down|got_ip|dhcp|reset`, `[ETH];link` 60-s heartbeat (link age, HB age, rx/tx max ms, renews, resets), `--ethstat`, `--ethdrop` (= `resetDHCP()`, timed: 126 ms on the bench). First 2-min window on DK5EN-90: `udp_rx` max 1 ms, `udp_tx` max 20 ms, no stall line — the 1.6–3.3 s `getUDP()` stall is not reproduced yet; the marker now names it when it occurs. `rak_harness.py --scenario instr` 2026-08-30: 60 s: one 803 ms gap in `hey_tx` (LoRa TX, not ETH); **600 s (`rak_instr600_20260830.json`): loop max 314 ms, 1 gap = the 15-min NTP round trip (`[ETH];stall;ntp;ms;213` in `eth_state`, NTPClient blocks up to 1 s by library design), 122 UDP RX with `udp_rx` max 5 ms / `udp_tx` max 32 ms, 0 `[ETH];stall` on the socket paths — the 1.6–3.3 s `getUDP()` stall is not reproduced in 12 min with the instrument in place.** Gate "loop max < 250 ms" fails only on the NTP round trip; bounding it means an async NTP on the shared gateway socket (note: `timeClient.begin()` re-binds the gateway `Udp` to port 1337 after `Udp.begin(1990)` — works because the server answers to the KEEP source port). Operator decision: accept the 15-min NTP block or build the async NTP. **DONE 2026-08-30 (async NTP built):** `src/ntp_async.{h,cpp}` -- `NtpAsync` sends the 48-byte request on the shared gateway socket and never waits; the reply is harvested by the normal receive path (`getUDP()` on nRF52, `getMeshComUDP()` on ESP32 offer every datagram to `tryConsume()` before parsing it as a MeshCom frame), with a 2.5 s timeout, 5 s/60 s backoff, mode-4/stratum/epoch validation and `[NTP];ok|timeout|txfail|kod` markers. This also removes a second defect: the stock `NTPClient::forceUpdate()` flushed _every_ queued datagram off the shared socket before sending, so each refresh could eat pending GATE/CONF frames. Both platforms wired, `NTPClient` no longer used. **Gate met:** `rak_harness.py --scenario instr --instr-seconds 600` (`rak_instr600_ntpasync_b_20260830.json`): **loop max 145 ms, 0 gaps > 250 ms** (was 314 ms / 1 gap), `eth_state` max 2.9 ms (was carrying the 213 ms NTP stall), and the 15-min refresh fell inside the window -- `[NTP];ok;epoch;1788084488;rtt;106`, 106 ms on the wire at zero loop cost. Native regression `test_ntp_async` 10/10 (non-blocking send, foreign datagrams left for the gateway, timeout backoff, server change, junk/kiss-of-death replies). A first 600 s window right after the flash showed two 1.7 s unattributed gaps in the boot/DHCP-retry window and 0.9 s `hey_tx` (LoRa TX) gaps -- both pre-existing families (N-20 / `sendHey`), absent from the steady-state run. |
| TM-36 | all ESP32 | TEST | Medium | `tools/bench/experiments/wifisoak.py`, `tools/bench/runs/wifisoak_W_20260830-112600/` | **WiFi soak evaluation (deferred by operator decision 2026-08-30: the remaining backlog items come first).** The 14-h soak on T-Deck/Heltec/T-Beam (`--wifidrop` every 10 min, held-open USB sessions, detached `nohup`) was started 2026-08-30 11:26 but **died at ~12:17**, when the TM-31 bench work took the Heltec and T-Beam USB ports (`tbeam: serial error: ... multiple access on port?` in `summary.txt`). ~51 min of data survived and is checked in: 5 drops, reconnect median 3 989 ms / p90 4 123 / max 4 123, 0 unsolicited disconnects, 0 BSSID changes, 0 `[WIFI];stall`, link RSSI -61/-57/-54 (n=42). **A full soak has to be restarted on a free bench** — it needs all three ports exclusively, so nothing else may run on them. To do: read the final summary, reduce with `wifisoak.py --parse-only wifisoak_W_*/{tdeck,heltec,tbeam}.log`, report SSID/BSSID/channel/RSSI per association, reconnect-time distribution after drops, unsolicited disconnects with reasons, BSSID changes, `[WIFI];stall` > 500 ms, unexpected resets. Bars: 0 unexpected reboots, every drop recovered, no `stall` > 500 ms, watchdog never reaches stage 2 on a healthy WLAN. Do not conclude from < 6 h. Report: [`wifi-report-20260830.md`](wifi-report-20260830.md). | **DONE 2026-08-31 — PASS on every bar** (`tools/bench/runs/wifisoak_night_20260830-224246/`, 9.1 h, drop every 600 s, GPS off so NTP was the clock, gateway off): 55/55 drops recovered per board (T-Deck median 4459 / p90 5037 / max 5329 ms; Heltec 4016/4086/4155; T-Beam 4032/5002/5221), 0 unsolicited disconnects, 0 watchdog actions, 0 `[WIFI];stall`, 0 unexpected resets (the one "reset" each on T-Deck/T-Beam is the port-open reboot at t=0), `same_ip` 55/55/55, every join WPA2-PSK (`pmf;0;wpa3;0`), BSSID re-picks between the two Orbi radios after drops (14/6/13) as designed, DNS 2 resolutions each max 114 ms — consistent with the Wave-W 51-min fragment (3989/4123/4123). Report: [`wifi-soak-report-20260831.md`](wifi-soak-report-20260831.md). **Side catch: NTP got 0 replies in 9.1 h on all three boards -> TM-45** (harvest gated on `bGATEWAY`; a WLAN-independent defect, does not touch this verdict). GPS restored `--gps on` on all three afterwards, verified by `--pos` (fix on each) |
| TM-37 | all | GAP | Medium | `loop_functions.cpp` `sendMessage()` (`user_msg`), `txring_functions.cpp` `addTxRingEntry()`, BLE/phone out path | **A dropped outgoing message is silent -- the sender is never told.** Confirmed 2026-08-30 while measuring TM-31: every outgoing frame, including the user's own, goes through the same 20-slot TX ring and is radiated one at a time, so three messages typed in quick succession are spooled and go out sequentially (bench drain under channel load: ~1 frame / 20 s; a quiet channel is much faster, the rate is set by airtime, CAD and channel occupancy). A user text is `MSG_PRIO_CRITICAL` (DM) or `MSG_PRIO_HIGH` (broadcast/group), so it normally evicts something lower -- but when the ring holds only equal-or-higher priority entries, `addTxRingEntry()` returns -1 (`RING_DROP_NEW`) and the message is gone. `sendMessage()` ignores that return value entirely; the only trace is a `[MC-DBG] RING_DROP_NEW` line that needs `--loradebug on`. The `[RING] overflow` print just above the call tests a different condition and goes to serial only. The phone/BLE user sees a message that looks sent and never was. | **DONE 2026-08-30 (as BP-01)** -- `addTxRingEntry()` return consumed in `sendMessage()`, QTA notice on drop |

| TM-38 | all four bench nodes | TEST | High | `tools/bench/experiments/wifisoak.py`, `tools/bench/rak_harness.py`, `udp_functions.cpp` `checkWifiPing()`/`wifiWatchdog`, `ntp_async.*`, `nrf52/nrf_eth.cpp` `ethLinkPoll()` | **The AP-reboot test -- the network case no soak covers so far (operator list 2026-08-30, item 7).** All four bench nodes log locally and unattended for 15 minutes; while that runs, the operator power-cycles the access points. One AP carries the RAK4631's LAN cable, so the RAK loses its Ethernet link in the same moment. Pass condition: every node re-associates on its own, has an IP again, NTP time is valid again, and the UDP link to the central server is back (KEEP/DATA leaving, server traffic returning) -- with no reboot, no serial command and no manual touch. Different from TM-36: there the link is dropped driver-side (`--wifidrop`) while the AP stays up; here the AP itself is gone for tens of seconds, so DHCP lease, DNS, ARP and the server socket all have to come back. Instrument: one held-open USB session per board on the bench Mac (`wifisoak.py` without `--drop-every`, plus a serial capture for the RAK) -- the 2323 net console dies with the WLAN and cannot be the witness. Assert `[WIFI];link;up`, `[WIFI];watchdog`, the NTP markers, `[KEEP]`, and on the RAK the `[ETH]` link edges from TM-35. | **DONE 2026-08-30 22:19 — real run PASS on all four boards** (`tools/bench/runs/apreboot_ap1_20260830-220423/`, operator at the APs, t0 22:08:49 = Heltec BEACON_TIMEOUT). ESP32 boards: link back +96-98 s, IP +98-99 s, server traffic +98-99 s after t0 — the Orbi reboot time dominates; driver auto-reconnect did it alone, 0 watchdog actions, 0 reboots, 0 commands. RAK: Ethernet link +9.6 s, DHCP re-ack +7.6 s, NTP +194 s — the AP switch ports return long before the WiFi radios. One anomaly: `[ETH];stall;ntp;ms;1609` at t0+65 s — the TM-35 "recurring stall" marker fired for the first time, 1.6 s loop block at the ntp site while the WAN was dead (DNS suspect), single occurrence, recovered on its own. Runner fix along the way: newest-run picker sorted by label, not timestamp (`ee0e3372`) |
| TM-39 | all gateway-capable | TEST/BUG | High | `udp_functions.cpp:1418-1480`, `nrf52/nrf_eth.cpp:1040-1200`, `command_functions.cpp:2223` (`--gateway srv`) | **Do the three country servers answer UDP the same way? (operator list 2026-08-30, item 8.)** `--gateway srv OE\|DL\|IT` is the only selector; the destination is then chosen at bring-up: HAMNET (own IP 44.x or `--hamnet on`) -> `meshcom.dig-italia.it` for IT, `meshcom.hamnet.cloud` for DL, literal `44.143.8.143` otherwise; internet -> `meshcom.dig-italia.it` for IT and `meshcom.oevsv.at` for everything else. **Reading already shows one asymmetry:** on the internet path `DL` is not special-cased at all, so a node set to DL without a HAMNET address sends to the Austrian server. Whether that is intended is unknown. Test per country: register a gateway, then compare across OE/DL/IT what comes back -- KEEP/heartbeat answer, whether the server pushes messages down, whether `{SET}`/`{CET}` arrive, timing, and what the node does when the server is silent. | **DONE 2026-08-30** -- `[GW];srv|rx|keep` markers both platforms, nRF52 `[UDP];rx/tx` + `--udplog` parity, `srvprobe.py` (24 reducer tests), live 3 x 180 s on DK5EN-93: OE and DL both resolve to `meshcom.oevsv.at` (the internet-path asymmetry is real), IT = `meshcom.dig-italia.it`; all three answer every KEEP with a 20-byte BEAT, 0 losses. Open: nRF52 knows `CONF`, ESP32 does not; nRF52 internet path has no per-country case at all. `docs/bench-country-servers.md` **Update 2026-08-31 (`b624bd33`, Wave 2): ESP32 now handles `CONF`** — pure bounds-checked parser `src/conf_frame.{h,cpp}` (12 native cases, env `native_conf_frame`), applied only from the resolved server IP, `--setcall` semantics (regex check, `save_settings()`, 15 s auto-reboot, T-Deck exception); coords parsed+logged, not applied. Note: on-wire coords are **little-endian** (the nRF52 producer's actual byte order), earlier docs said big-endian. nRF52 apply-defect filed as CONF-01.|
| TM-40 | ESP32 WiFi nodes | TEST | Medium | `tools/webflash.py`, `tools/ensure_safeboot.py`, `src/safeboot/`, `docs/automation-runner-runbook.md` | **OTA flashing belongs in the regression run (operator list 2026-08-30, item 10).** Every WiFi-capable node must prove it can be re-flashed over the air, unattended: safeboot partition reached, image uploaded, node reboots into the new build, `--info` reports the new version, WLAN and the UDP link come back, and the settings survive the update. Covers T-Deck Plus, Heltec V3 and T-Beam -- the RAK4631 has no WiFi, its update path stays UF2/DFU over USB and is out of scope here. Note the T-Beam needs `--webserver on` before the OTA endpoint exists (bench memory), and a failed OTA must leave the node bootable, which is exactly what safeboot is for. | **DONE 2026-08-30** -- `tools/webflash.py` importable (`flash()` -> `OtaResult`, env->hardware map, prefix-tolerant `TBEAM`/`TBEAM_AXP2101`), `tools/bench/ota_regression.py` (+10 unit tests), live PASS on DK5EN-92: safeboot -> new image -> `[BOOT];ready` -> WLAN back, settings snapshot identical. `docs/bench-ota-regression.md` |
| TM-41 | T-Deck Plus | TEST | Medium | `src/t-deck/tdeck_debug.cpp`, T-Deck flush path (`tdeck_main.cpp`), `tools/bench/tdeck_harness.py` | **Colour and geometry display test, driven blind by the harness (operator list 2026-08-30, item 11).** Sequence: invert the whole screen; full-screen fills red, yellow, green, blue, magenta in quick succession; one inverted pass over the same set so the complementary colours show; then a black square growing from the centre, one pixel per step, until the screen is full; then a white circle growing from the centre one pixel per step; then a triangle in the centre, rotating three times clockwise and three times counter-clockwise. **Verification is driver-side** (operator decision 2026-08-30): `--screencrc` is void on this panel (MISO not driven, constant readback -- `tdeck-findings-20260828.md`), so the assertion has to sit on the flush path -- CRC or pixel probe of the buffer actually handed to the panel, with full refresh for the fills because under partial refresh `draw_buf` only holds the last rectangle. That proves the right pixels were sent, not that the glass shows them; an operator may watch, but the test runs the whole sequence unattended either way. | **DONE 2026-08-30** -- `--disptest [phase] [stride]` renders every frame in RAM, CRC32 of the bytes handed to `pushColors()`, `[DISPTEST];step` per frame; Python reference renderer in `tdeck_parse.py`; harness scenario `disptest`: 516/516 CRCs match at stride 1, 42 s, 12.2 fps (loop-task WDT fed per frame -- first run rebooted at frame 61). `docs/tdeck-display-test.md` |
| TM-42 | all | TEST | Low | `regex_functions.cpp:35`, `src/test_inject.*`, `tools/bench/`, `docs/automation-runner-runbook.md` | **Use group `TEST` for tests, not only `9999` (operator list 2026-08-30, item 9).** `TEST` and `TESTER` already pass `checkRegexCall()`, so the firmware accepts them as a destination today; the central server filters that group, so test traffic sent over LoRa or with `--gateway on` disturbs nobody. Work: make `TEST` the default destination of the frame injector and the bench scenarios (`test_inject.h:22` documents `9999` today), write the convention into the runbook, and prove it once end-to-end -- a `TEST` message sent from a gateway node leaves the node (own log) and does **not** show up on the map/dashboard. | **DONE 2026-08-30** -- `TEST` default in `test_inject.h`, `tdeck_harness.py`, `oled_harness.py`; convention in the runbook; proven: `::{TEST}...` left DK5EN-93 with `--gateway on` (`[UDP];tx` len 107) and is absent from mcmap while neighbouring 99099 traffic of the same minute is there |
| TM-45 | all (ESP32 + nRF52) | BUG | Medium | `src/esp32/esp32_main.cpp:3708` (`if(bGATEWAY && …)` around `getMeshComUDP()`), `src/nrf52/nrf52_main.cpp:1963` (`if(bGATEWAY)` around `neth.getUDP()`), `src/udp_functions.cpp:172` (`tryConsume()`), `src/ntp_async.cpp` | **NTP never succeeds on a non-gateway node — the reply is sent for but never read (TM-35 regression, found by the TM-36 night soak, the first run with NTP as the only clock).** The soak logged **0x `[NTP];ok` and 545–548x `[NTP];timeout` per board in 9.1 h** (60-s backoff cadence all night, to the resolved `5.1.73.50`; the 2–3 `txfail` per board all fall into drop windows). Mechanism: the refresh path runs on any node with an IP and no GPS fix (`esp32_main.cpp:2628`, every 15 min, un-gated) and `NtpAsync::sendRequest()` transmits fine — but since TM-35 the reply is harvested only by the normal UDP receive path (`getMeshComUDP()` -> `tryConsume()`), and that path sits inside the gateway block on **both** platforms. The datagram stays unread in the socket, every attempt hits the 2.5 s timeout. Before TM-35 the blocking `NTPClient::forceUpdate()` read the socket itself, so gateway-off nodes did get time. Field impact: a WiFi node with GPS off and gateway off **never has a valid wall clock** (mheard aging, beacon `/N` count and timestamps run on garbage — cf. NC-01) and uselessly sends one NTP datagram per minute forever. Fix direction: poll the shared socket for the NTP reply outside the gateway gate (cheapest: call a small harvest step whenever `timeClient` has a request pending and `bGATEWAY` is off — it must offer non-NTP datagrams to nothing, or drain them, since no gateway consumer exists), or run `getMeshComUDP()` un-gated and keep only the TX/heartbeat side gateway-gated; nRF52 needs the same decision for `neth.getUDP()`. Add a native regression: gateway-off node, pending request, reply datagram arrives -> time set. Bench proof: DK5EN-93 gateway off, GPS off -> `[NTP];ok` within one refresh. | **FIXED 2026-08-31** (`81cfc064`, Wave 1): harvest-only step `ntpHarvestReply()` (template in `ntp_async.h`) called from the `else` branch of both gateway blocks (`ntpHarvestUDP()` ESP32, `NrfETH::harvestNTP()` nRF52) — structurally exclusive with the gateway read, non-NTP datagrams drained. Native regression `test/test_ntp_harvest/` (4 cases). **Bench proof 2026-08-31:** DK5EN-93 (gateway off, GPS off, reboot) -> `[NTP];ok;epoch;1788161341;rtt;89`, 0 timeouts; GPS afterwards restored |
| TM-46 | ESP32 safeboot | BUG | Low | `src/safeboot/`, `tools/webflash.py` | **Safeboot recovers dirty from an aborted OTA upload.** Found 2026-08-31 flashing the release: on the T-Deck (largest image of the fleet, 2.17 MB, WLAN at the documented -74 dBm margin) the upload stalled and the safeboot abandoned the session mid-transfer and rebooted into the app (anti-brick fallback, tool saw the OLD build come up, then `Errno 32`). Correct so far — but the SECOND attempt then got **HTTP 400** on a fresh `/ota/start`: the aborted session leaves state behind, and the node was still serving the OTA page minutes later. A safeboot should accept a clean new session after any abort. Repro: interrupt an OTA upload, retry. USB flash is the workaround (used for DK5EN-14). | **IMPLEMENTED 2026-08-31** — `abortActiveUpdate(reason)` centralizes cleanup (`Update.abort()` + `[SAFEBOOT];ota;abort;reason;<stale_session\|write_failed\|client_disconnected\|stalled>`), fired from a fresh `/ota/start` on a stale session, the write-failure path, an `onDisconnect` hook, and a 30-s no-data stall watchdog in `loop()`; the abort/end callbacks clear `updateInProgress` and re-arm the fallback-to-app timer. Follow-ups from the bench: session **generation counter** so a lingering old connection's late disconnect cannot kill a fresh session, explicit `fallback_armed_at` timestamp (the old timer compared raw uptime and the re-arm was a no-op), and the decisive find — a **cross-task unsigned-underflow race**: `loop()` could read `millis()` just before the async_tcp task stored a newer `last_ota_data_millis`, the unsigned delta wrapped to ~2^32 and the stall watchdog aborted every healthy upload seconds in (serial-proven: `abort;reason;stalled` in the same ms as the first progress callback). Fixed with signed-delta arithmetic + `volatile`. **Bench-proven 2026-08-31 on DK5EN-14 (2x)**: upload killed 5 s in -> node back in the app within seconds, immediate retry completes a full OTA without `--force` |
| TM-47 | tools | BUG | Low | `tools/webflash.py` (`ENV_HARDWARE`) | **`webflash.py` hardware check blocks T-Deck Plus:** the map expects `TDECK_PLUS` for env `t_deck_plus`, but the firmware self-reports `TDECK` (web UI shows `TDECK+`) — every legitimate flash needs `--force`, which defeats the wrong-board guard exactly where boards are most similar. Align the expected string with what the firmware actually reports. | **FIXED 2026-08-31** — two defects: the `node_info()` regex dropped `+` (tool saw `TDECK` on a page showing `TDECK+`), and the map expected `TDECK_PLUS`, which only exists in the default hardware table that never compiles for T-Deck builds (`mheard_functions.cpp:84` vs `:86`). Parser keeps `+`, map says `TDECK+`, mismatch error hints at formatting-only differences, new `--self-test` with embedded fixtures; plus a safeboot-resume path (a node already sitting in safeboot is recognized via the OTA page title, hardware check and trigger are skipped) and HTTP-400 bodies are no longer dropped from upload errors. Firmware untouched. **Bench-proven 2026-08-31**: repeated full OTAs on DK5EN-14 without `--force`, `TDECK+` accepted |
| TM-48 | ESP32 safeboot | BUG | Medium | `src/safeboot/main.cpp:116-167` | **Safeboot carries the pre-TM-34 WLAN implementation** — the same association defects fixed in the production firmware 2026-08-30: no `esp_wifi_disable_pmf_config` (SAE-without-PMF trap on WPA2/WPA3 transition APs, was 24/24 AUTH_EXPIRE), own scan with BSSID/channel pinning (`WiFi.begin(..., channel, bssid, true)` — the exact anti-pattern the fix removed in favour of driver-owned selection), blocking 15-s join loop that then falls back to **AP mode**, making a failed first join unreachable over the LAN for the OTA tool. Plausible co-cause of the TM-46 upload abort (pinned join at the -74 dBm desk margin). Fix: port the TM-34 join pattern (PMF-off config, driver-selected AP, no pinning); keep the AP fallback as last resort but only after a real retry. | **IMPLEMENTED 2026-08-31** — production pattern ported to `wifiConnect()`: `persistent(false)`/auto-reconnect/all-channel scan/signal sort, own scan + both pinned `WiFi.begin()` calls removed, config-only begin -> `esp_wifi_disable_pmf_config` (`[SAFEBOOT];wifi;pmf_off;rc`) -> `esp_wifi_connect()`; "empty"/"none" both open; blocking wait extended to 25 s with one retry at 12 s, AP fallback only after that. **Bench-proven 2026-08-31**: `[SAFEBOOT];wifi;pmf_off;rc;0` on the serial, join + mDNS up in seconds, 2.17 MB uploads at ~170 kB/s on the -74 dBm desk link |
| TM-49 | ESP32 safeboot | BUG | Medium | `src/safeboot/ElegantOTA.cpp` (upload completion handler), `main.cpp` `onOTAEnd()` | **A disconnect mid-upload can reach the completion handler with `hasError == false`** — the `final` frame was never seen, `Update.end()` never ran, so nothing set an error; the handler may then set `_reboot` and `onOTAEnd(success=true)` runs `setBootPartition_APP()` after a PARTIAL write. Observed indirectly on the bench (node back in the app seconds after a killed upload instead of via the 180-s fallback). On the 16-MB T-Deck the bootloader's slot validation saved it; on a 4-MB single-slot board (Heltec) this could switch to a half-written app image. Fix direction: the completion path must treat "final never received" as an error (track a `sawFinal` flag; guard `_reboot` and the partition switch on it), and `onOTAEnd(success)` must only switch partitions when `Update.isFinished()` confirms a complete image. Needs a bench proof on a 4-MB board before the next release. | open |
| TM-50 | Tooling | BUG | Low | `tools/meshlogger.py` | **meshlogger holds a zombie TCP connection after the target node's WLAN drops.** Overnight soak 2026-09-01: router reboot at 05:35 killed the node side of the 2323 session; the logger kept a half-open socket for 2.4 h (`reconnects=0`, no data, gap 05:35-07:59), noticed nothing until the final flag-restore hit `Broken pipe` -- so `--loradebug` on the node was NOT restored either. Fix direction: enable TCP keepalive on the socket AND treat read silence longer than ~90 s as dead (the node emits periodic output with flags on), then reconnect and re-apply flags; count real reconnects. The serial-redundancy channels absorbed the data loss this time (`docs/wifi-soak-report-20260831.md`, Nachtrag). | **DONE 2026-09-01** -- read-silence watchdog (`--stall-timeout`, default 90 s) raises through the existing reconnect path, SO_KEEPALIVE + platform tunables as belt-and-braces, flags re-applied after every reconnect, end-of-run flag restore retries once on a fresh connection. Regression test `tools/bench/test_meshlogger.py` replays the incident against a fake console (zombie phase, second live connection); fails-before verified against the pristine pre-fix code |

#### Bench fleet (scanned 2026-08-29, all four live on USB)

**LoRa TX power 2 dBm on all four, set 2026-08-30** (`--txpower 2`, takes effect at once via
`lora_setchip_meshcom()`, persisted): T-Deck 22 -> 2, T-Beam 20 -> 2, Heltec -> 2, RAK -> 2.
Read back with `--lora` on each node. **Production node: `DK5EN-98`** (operator, 2026-08-30) -- set to `--gateway off`, `--mesh off`,
2 dBm over the web API (`/setparam/?gateway=off` etc., all three `returncode:0`), confirmed on
its info page: `Gateway: off / Mesh: off / TX Power: 2 dBm (1.58 mW)`. **`DK5EN-93`** (bench
Heltec, was itself registered as a gateway) is likewise `--gateway off`, `--mesh off`, 2 dBm.
Both have to go back afterwards -- note the state before the test was: DK5EN-98 gateway on,
mesh on, and DK5EN-93 gateway on, mesh off.

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

| ID    | File                                     | Verified finding                                                                                                                                                                                                                                                                                 | Sev.   | Action                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ----- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| UP-01 | `mheard_functions.cpp` (2 sites)         | `serializeJson` bound is the JSON's length, not `sizeof(bleBuffer)-1`. Worst case today 284/299 B (15 B headroom) — latent, goes live with the next key.                                                                                                                                         | Medium | **FIXED `b586daee`** — `bleJsonFrame()` + `test_ble_json_frame`; goes into the PR.                                                                                                                                                                                                                                                                                                                                                                                                                         |
| UP-02 | `lv_obj_functions.cpp` `add_map_point()` | `delay(40)` x2 in `add_map_point()`, reached from `OnRxDone` per position beacon (80 ms) and 30x from `refresh_map()` in one click callback (2.4 s). On our tree the G01 hunk had already removed the pair; one `delay(10)` in the slot-recycling branch (after 30 on-screen stations) remained. | High   | **FIXED 2026-08-29** — last delay dropped; `add_map_point()` is delay-free. Harness `map --map-stations 40` exercises the recycle branch (wrap to `map_points` 11, 40/40 injected, no crash); loop max in the injection window 124 ms before / 153 ms after = noise, the win is structural (no blocking on the RX path). Harness fix on the way: station calls are `DK5EM-nn` — `DK5EN-14` collided with the bench node's own call and each collision recomposed the map (728 ms loop stall, an artefact). |
| UP-03 | `tdeck_sdmap.cpp:304-305`                | x scaled by 320-32, y by 256, image zoomed 1.25x: markers wrong in y at every zoom. Magic numbers.                                                                                                                                                                                               | High   | Ours (`db298c49`) wins the merge; two hunks merge silently — check by hand.                                                                                                                                                                                                                                                                                                                                                                                                                                |
| UP-04 | `lv_obj_functions.cpp:1758,1766`         | G01 NULLing — Kurt fixed it independently, same as ours.                                                                                                                                                                                                                                         | —      | Keep his hunk; drop ours from the PR.                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| UP-05 | `command_functions.cpp:4961`             | `I` register at 239/244 chars with six group calls.                                                                                                                                                                                                                                              | Low    | Watch.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| UP-06 | `regex_functions.cpp:9` (pre-existing)   | Callsign regex `[0-9]+` unbounded — 119-char callsigns pass validation.                                                                                                                                                                                                                          | Medium | Trace consumers, small PR + test.                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |

**Process rule from here on:** every `git merge upstream/dev` into fork main is preceded by a
review of the _net_ diff since the last merge base (`git diff <base> upstream/dev`), findings
filed as `UP-nn` here and in `08-defect-catalogue.md`. Upstream has no CI and no tests; our
native suites are the only gate MeshCom code passes through, so upstream changes in covered
areas get a test added at merge time.

**Plan agreed 2026-08-29:** (1) docs, commit, push; (2) tag and delete stale branches (§4);
(3) `/fable-review` on `fc83554e..upstream/dev`; (4) **done 2026-08-29** — `upstream/dev` merged into
`tdeck-partial-refresh-trace` (resolution in the merge commit message; native 45/45, four targets build);
(5) bench-verify on DK5EN-14; (6) T-Deck PR built from `upstream/dev` per §4, UP-01 fix included.

### 3.8h Configuration surface — max hop, and config backup/restore (intake 2026-08-30)

From the operator list of 2026-08-30 (items 1-3). Same theme as the closed §3.8a: a setting is only
usable if it is reachable **and** survives a reset — over serial, over the web GUI, and in NVRAM.
The rule from HL-01/HL-03 holds for everything here: **the GUI calls `commandAction()` with the
same command string**, so GUI and serial cannot drift apart.

| ID    | Type | Sev.   | Location                                                                                                                   | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| ----- | ---- | ------ | -------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CS-01 | GAP  | Medium | `command_functions.cpp`, `esp32/esp32_flash.cpp` (load/save), `esp32/esp32_main.cpp:935`, `nrf52/nrf52_main.cpp:602`       | **Serial command for the hop limit, persisted in NVRAM.** New `--maxhop <1..6>` setting `meshcom_settings.max_hop_text`. Scope decided 2026-08-30: **only the text hop count** is user-settable; `max_hop_pos` stays at its compile default (`MAX_HOP_POS_DEFAULT` = 2). Nothing persists today — the two fields live in the settings struct but the ESP32 NVS layer has no key for them at all, and **both platforms overwrite them with the compile-time defaults on every boot**, so even the nRF52 (which writes the whole struct to flash) discards the stored value at the next start. | **DONE 2026-08-30** -- `--maxhop <1..6>`, NVS key `max_hop_text`, clamp in `settings_sanitize`, `[MAXHOP];text;N;pos;N` marker; verified: Heltec keeps 2 across a reset, RAK sets 3, 7/0 rejected                                                                                                                                                                                                                                                                                                                                                                           |
| CS-02 | GAP  | Medium | `web_functions/web_setup.cpp` (`webSetup_setParam`), `web_functions/web_functions.cpp:1074` (`<select>` precedent)         | **Max-Hop as a drop-down on the web Config page.** Offered values 4, 3, 2 — but the field must also **display 5 or 6** when the serial command set one of those, i.e. the current value is added to the list when it falls outside it. The handler goes through `commandAction("--maxhop <n>")` (CS-01), like every other parameter in `webSetup_setParam`; a `<select>` already exists for `country` and is the pattern to copy. The wider range on serial vs. the narrow one in the GUI is deliberate (operator, 2026-08-30).                                                              | **DONE 2026-08-30** -- `<select name="maxhop">` 4/3/2 + current, `/setparam/?maxhop=` and `/getparam/?maxhop` verified on DK5EN-93 (9 rejected with returncode 1)                                                                                                                                                                                                                                                                                                                                                                                                           |
| CS-03 | GAP  | Medium | `web_functions/web_functions.cpp` (Config page), `esp32/esp32_flash.cpp`, `nrf52/nrf52_flash.cpp`, `command_functions.cpp` | **Config download/upload as JSON.** A download button on the Config page yields the node configuration as one JSON object; a later upload writes it back and the node reboots once with the restored config. The file must carry the **flash/NVRAM layout version**, so an import into an incompatible layout can be refused instead of half-applied, and a **hash**, so a hand-edited or corrupted file is rejected. This is the bulk form of the §3.8a goal — provisioning a node without a human at the display.                                                                          | **DONE 2026-08-30** -- `src/config_json.{h,cpp}` (X-macro field table, NVS key names, CRC32 over a canonical text, `layout` = `FLASH_STRUCT_VERSION`), `GET /config.json` + `POST /config` with a bounded body reader, 14 native cases. Verified on DK5EN-93: download 107 fields, bad crc / bad layout / out-of-range / truncated / oversized all 400 with nothing applied, re-import reboots and comes back with the same settings. Not exported: msg-id counters and live sensor readings (gate decision); GPS lat/lon/alt still drift between two exports of a GPS node |
| CS-04 | BUG  | Low    | `web_functions/web_functions.cpp` (HTTP GET parsing)                                                                       | **`/getparam/?<name>` is broken.** `/setparam/?gateway=off` works and answers `{"returncode":0, "gateway":"off"}`, but the matching read `/getparam/?gateway` answers `{"returncode":2, "ram/?gateway":""}` — the parameter name is cut out of the wrong offset of the URL, so every read is "parameter unknown". Found 2026-08-30 while setting DK5EN-98 remotely; the only working read-back today is scraping the info page.                                                                                                                                                              | **FIXED 2026-08-30** — two wrong literals in two lines, detail below                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |

**CS-01, what the work actually is.** `max_hop_text` / `max_hop_pos` are members of
`s_meshcom_settings` (`esp32_flash.h:209`, `WisBlock-API.h:375`), but `esp32_flash.cpp` neither
reads nor writes them — there is no `preferences` key. On both platforms `setup()` assigns
`MAX_HOP_TEXT_DEFAULT` / `MAX_HOP_POS_DEFAULT` unconditionally after the settings are loaded
(`esp32_main.cpp:935`, `nrf52_main.cpp:602`), which is what makes the value effectively a
compile-time constant today. So: add the NVS key plus load/save, turn the two assignments into a
default that only applies when nothing valid is stored, add the command with a 1..6 clamp and a
raw-`Serial.printf` marker so the bench harness can read it back, and cover the clamp in the
native suite.

**Open decision — precedence against the server.** `{SET}` from the central server changes
`max_hop_text` / `max_hop_pos` at runtime today (`loop_functions.cpp:2283`, range-checked against
`MAX_HOP_LIMIT` = 7 since this campaign). Once the operator's value is persistent the two can
disagree. Proposal, needs the operator's OK: **`{SET}` keeps working at runtime but is never
written to flash**, so a reboot restores the locally configured value and the server can still
steer the network in an emergency. The alternatives are "server wins permanently" (persist it) or
"local value locks the server out".

**CS-03, what exists and what has to be invented.**

- **There is no config version today.** The ESP32 stores every setting as its own NVS key in the
  `Credentials` namespace (`esp32_flash.cpp`), so there is no struct version at all; the nRF52
  writes the whole struct to a file behind a 2-byte marker plus a `sizeof` check (`N-12`) and has
  exactly one legacy migration path. The export therefore needs an explicit, newly introduced
  config-format version — the firmware version alone is not enough, because the layout changes
  independently of it.
- **There are partial JSON views already** — the phone/BLE settings frames (`TYP:"SN"`, `"AN"`, …
  in `command_functions.cpp:5988ff`) are a good model for field naming, but none of them is
  complete.
- **Open decision — what the hash is for, and what goes into the file.** A plain hash
  (CRC32/SHA-256) makes the file tamper-**evident** only: anyone who edits the JSON can recompute
  it. Real forgery protection needs an HMAC over a secret the node knows — which one (web
  password? BT code? a fleet key?) is undecided, and a per-node secret makes a config
  non-transferable between nodes. Second half of the same decision: do secrets travel in the
  export (`node_pwd` = WLAN password, `node_webpwd`, `bt_code`)? Including them makes the restore
  complete and puts the WLAN password in clear into a downloadable file; excluding them means an
  import cannot fully bring a node back.
- **Acceptance:** export → import on a wiped node → export again must be identical, and an import
  with a wrong version or a wrong hash must be refused with a visible reason and must not touch
  NVRAM.

---

**CS-04, what was wrong.** `getparam()` (`web_functions/web_functions.cpp`) carried two defects in
two consecutive lines, both reproduced live on `DK5EN-98` before the fix:

1. it searched the request for `"/setparam/?"` instead of `"/getparam/?"`. `indexOf()` then returns
   `-1`, and `substring(-1 + 11 = 10, …)` cut the name out of the wrong offset — `GET
/getparam/?gateway HTTP/1.1` yielded the parameter name `ram/?gateway`, hence
   `{"returncode":2, "ram/?gateway":""}` for every read;
2. with a `=` present it took `substring(indexOf("="))`, i.e. the part **from** the equals sign,
   while its own comment says it wants the name — `/getparam/?gateway=` yielded the name `=`.

So the entire read half of the Web-API was dead while `/setparam/` worked; the web GUI never
noticed because it renders values into its pages instead of calling `/getparam/`. Fixed by using
the right literal and `substring(0, indexOf("="))`. **Verified on hardware** (DK5EN-93,
192.168.68.66): `/getparam/?gateway` → `{"returncode":0, "gateway":"off"}`, `?mesh` → `"off"`,
`?txpower` → `"2"`, and the trailing-`=` form now answers correctly too.

---

### 3.8i What the central server learns from a node (intake 2026-08-30)

From the operator list of 2026-08-30 (items 4-6). Both threads are about the same question: the
node holds information that never arrives at the central server.

| ID     | Type | Sev.   | Location                                                                                           | Item                                                                                                                                                                                                                                                                                     | Status                                                             |
| ------ | ---- | ------ | -------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| GW-01  | BUG  | High   | `loop_functions.cpp:4334` (`sendHey()`), `lora_functions.cpp:1194-1211`, `aprs_functions.cpp:1127` | **HEY feature parity: with `--gateway on` the neighbours never reach the server.** Operator, 2026-08-30: the node's own web GUI shows the complete mheard list, but that information does not arrive at the central server; with `--gateway off` the HEY-path data shows up there again. | **FIXED 2026-08-31 (fix a), bench- and server-proven** — see below |
| TLM-01 | GAP  | Medium | `loop_functions.cpp` `sendTelemetry()`, `esp32_main.cpp:3313`, `nrf52_main.cpp:1943`               | **Telemetry definition over LoRa at regular intervals**, not only the values.                                                                                                                                                                                                            | **parked 2026-08-30** — blocked by TLM-03                          |
| TLM-02 | GAP  | Medium | same, plus `udp_functions.cpp` `addNodeData()`                                                     | **Telemetry definition over UDP at regular intervals**, not only the values.                                                                                                                                                                                                             | **parked 2026-08-30** — blocked by TLM-03                          |
| TLM-03 | BUG  | High   | `configuration_global.h:303`, `loop_functions.cpp:4402-4420`, `tinyxml_functions.cpp:230`          | **The soft-serial telemetry path has to be examined before either of the two.** Operator decision 2026-08-30. Finding below: on a node without a soft-serial measuring station the telemetry sender is dead code.                                                                        | open — precondition for TLM-01/02                                  |

#### GW-01 — why a gateway may publish less than a plain node

What the code says, read 2026-08-30 (not yet measured):

- `sendHey()` builds the node's **own** HEY with payload `R<ncnt>;` and **no signal report** —
  reports are appended by the _receiver_ (`appendHeySignalReport()`), never by the sender.
- On a gateway that frame goes out twice: straight to the server via `addNodeData()`, **and** to
  the LoRa TX ring.
- A neighbouring gateway that hears the same HEY over the air appends its own `NCT,RSSI,SNR`
  before uploading it (`lora_functions.cpp:1194`, "HEY always send to Server").
- Both uploads carry the **same `msg_id`**. If the server keeps the first copy it sees, the
  report-less self-upload wins over the enriched copies from the neighbours — the node is
  registered, the link data is lost. With `--gateway off` the self-upload does not exist, so only
  the enriched copies arrive. That matches the operator's observation exactly.
- The receiving direction is fine: a gateway does append its own report to foreign HEYs before
  uploading them, and the local mheard/HEY-path view is updated for **every** received copy
  (`lora_functions.cpp:708`), including duplicates that the dedup gate at line 744 later drops —
  which is why the web GUI looks complete while the server does not.

**Decisive experiment before any code change:** DK5EN-93 as gateway, capture UDP-out with the
TM-31 instrument and compare against what mcmap holds for the same `msg_id`; then repeat with
`--gateway off`. Confirm or refute the dedup hypothesis first — the upload path itself is
unconditional for `'@'`, so the loss is either at the server or in the copy that wins.

Fix candidates once confirmed: (a) do not self-upload the own `'@'` frame and let the neighbours
report it, exactly as `--gateway off` does — smallest change; (b) self-upload with an explicit own
report group so the server sees a complete record; (c) upload mheard as its own record type —
protocol change, needs the server side.

**Measured 2026-08-31 (dual-node bench run, DK5EN-93 source / DK5EN-92 observer gateway, both
`--debug on --loradebug on --udplog on`; mcmap `interlink` log as server-side reference):**

- The first-copy-wins-at-ingress hypothesis and a UDP-echo dedup race are both **refuted**: the
  server does not echo `'@'` frames back to gateways at all (0 `[GW];rx;type;DATA` around the HEY
  on the observer), so the observer's dedup ring is never poisoned — the RF copy arrives as
  `RX_DEDUP_NEW`, gets enriched and uploaded even with the source's gateway on.
- Phase A (gateway ON, msg_id `EA25A2BB`): **three** records in the server's interlink stream —
  `rssi:"" gw:1` (the bare self-upload, first by ~3 s: wire beats TX-ring latency 2.4 s + airtime)
  then two enriched `gw:0` records (`4,37,7;` RAK-90, `3,36,12;` T-Beam-92). Same triple pattern
  on every boot/trickle HEY (`EA25A2B1..BC`).
- Phase B (gateway OFF, msg_id `EA25A2BD`): exactly the two enriched `gw:0` records.
- Operator statement 2026-08-31 confirmed empirically: the server stream carries **no msg_id
  dedup for HEY frames** — every copy is distributed.
- Conclusion: the firmware loses nothing; the only gateway-on difference is the report-less
  `gw:1` copy that always arrives first. Any consumer that keeps the first (or the gateway's own)
  record per msg_id shows exactly the operator's symptom. That consumer is server-side; fix (a)
  removes the bare first copy from the firmware side and restores parity with `--gateway off`
  regardless of server behaviour.
- Side observation: with gateway on the HEY destination is `HG`, with gateway off `H`
  (`DK5EN-93>HG@R2;` vs `DK5EN-93>H@R2;`).

**Fix (a) shipped and verified 2026-08-31** — `sendHey()` no longer calls `addNodeData()` for the
own `'@'` frame (`loop_functions.cpp`, GW-01 comment). Verified on the bench (DK5EN-93 fix build,
gateway ON, DK5EN-92 old-build observer) for both the boot HEY `EA25A2BF` and the `--sendhey` shot
`EA25A2C0`: the source's only non-KEEP uploads are its enrichments of foreign HEYs; both own HEYs
radiate, the observer logs `RX_DEDUP_NEW` and uploads enriched — and the mcmap interlink stream
carries **only** the two enriched `gw:0` records per msg_id, exact parity with `--gateway off`.
The receiving direction on the fix build is intact (93 uploaded `DK5EN-92>HG@R3;2,30,7;`).
No native regression test: `sendHey()` sits in `loop_functions.cpp`, which no native env links;
the pin is the bench procedure above plus this record.

#### TLM-03 — the telemetry sender is dead on a node without a measuring station

- The periodic sender calls `sendTelemetry(SOFTSER_APP_ID)` on both platforms
  (`esp32_main.cpp:3313`, `nrf52_main.cpp:1943`), and so do the manual commands
  (`command_functions.cpp:3085`, `:3095`). `SOFTSER_APP_ID` is a compile-time `1`
  (`configuration_global.h:303`).
- `sendTelemetry()` therefore always takes its `ID == 1` branch, which **returns immediately
  unless `node_parm_1` is non-empty** — and that field is written only by the soft-serial XML path
  (`tinyxml_functions.cpp:230`). The `else` branch, the one that uses the node's own `node_parm`,
  is unreachable in the shipped firmware.
- Consequence: on a node without a soft-serial measuring station **nothing is sent at all** —
  neither values nor `PARM`/`UNIT`/`EQNS` — while `--parm`, `--unit`, `--values` and `--ptime`
  accept and persist settings that never go on air.
- In that same branch the station callsign is `strSOFTSERAPP_ID`, which is empty without the XML
  path — a telemetry frame under an empty callsign.
- What already exists, and will decide TLM-01/02 once the path is alive: `sendTelemetry()` walks a
  24-slot rotation (0 = `PARM`, 1 = `UNIT`, 2 = `EQNS`, 3 = `BITS`, 4…23 = values, then wrap), so
  the definitions do go out — roughly every 6 h at the default `PTIME` of 15 min, and right after
  a reboot because the counter is not persisted. The transport, however, is **exclusive**: a
  gateway sends telemetry only over UDP, a non-gateway only over LoRa
  (`loop_functions.cpp:4662-4674`). That exclusivity is the actual subject of items (4) and (5).

---

### 3.8j Beacon flood, parser test coverage, wire-format tails (intake 2026-08-30, second list)

| ID    | Type | Sev.   | Location                                                                               | Item                                                                                                                                                    | Status                                                                                                                                                                                    |
| ----- | ---- | ------ | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| FL-01 | BUG  | High   | `loop_functions.cpp` `sendPosition()`, `extudp_functions.cpp:211`, new `beacon_rate.h` | A node can be driven to originate position beacons at **loop rate** (~20/s). Measured in the field: 25 146 frames in 21 minutes from one station.       | **FIXED 2026-08-30** — 30 s floor on the shot path                                                                                                                                        |
| PT-01 | TEST | High   | `test/`, all decode entry points                                                       | Protocol-parser test coverage audit: which parsers exist, which are tested, and whether the tests go past the happy path (overrun, control bytes, NUL). | **DONE 2026-08-30** -- three native envs (`native_parsers`, `native_extern`, `native_xml`), 58 cases; 8 real parser findings pinned with `TEST_IGNORE_MESSAGE` (see PT-01 findings below) |
| WF-01 | BUG  | Medium | `phone_commands.cpp:125,199`; `udp_functions.cpp:1599`, `:1565`                        | Frames carry trailing bytes past their own payload: BLE notifications send `blelen + 2`, the UDP `KEEP` frame includes its string terminator.           | site 3 **FIXED 2026-08-30**; sites 1+2 parked (wire format)                                                                                                                               |

#### FL-01 — the replay bursts are not replays

Trigger: [`mcmap docs/findings/interlink-frame-replay-bursts.md`](../../mcmap/docs/findings/interlink-frame-replay-bursts.md)
(18 events, 18 callsigns, six hardware families, three firmware states). That report reads the
signature as "a 1000-entry ring buffer is being replayed upstream of us" and concludes the cause
cannot be located from mcmap's side. **Two of its premises are wrong, and correcting them puts the
cause inside the node firmware.**

Measured 2026-08-30 against the live `interlink` log (mcmap MCP, `logs_grep`), event
`DL6MDF-11` / 2026-08-30 04:20 CEST:

- **"Exactly 1000 distinct `msg_id`s" is not a buffer size — it is the node's whole ID space.**
  `msg_id = (_GW_ID << 10) | (node_msgid & 0x3FF)` with `node_msgid` wrapping at 999
  (`loop_functions.cpp:3088,3101`). A station that keeps sending visits all 1000 values and no
  more. The gapless block `4AB16000`–`4AB163E7` is therefore evidence of **many sends**, not of a
  1000-slot queue.
- **The copies are not copies.** The 25 occurrences of `msg_id 4AB16000` arrive 50–51 s apart —
  one per lap of the counter — and **their battery field differs from frame to frame** (88, 92,
  88, 100, 91, 93, 100, 93, 87, 100 %). A replayed buffer cannot vary its own payload. Every
  frame was freshly built, with a live ADC reading in it.
- Consequently the finding's third argument ("the real counter is behind the burst") is void: the
  counter is only 1000 wide, so _every_ ID is always inside the "already used" range.

So the node originated ~25 000 position frames in 21 minutes — 1000 IDs × ~25 laps, ~20 frames/s
— and uploaded each one to the server itself (`gw:1`, `rssi:0`, `snr:0` is what `addNodeData()`
passes for a node's **own** frame, `udp_functions.cpp:1618`). Its regular cadence is one frame per
30 minutes. Over LoRa such a rate is impossible (airtime), which is exactly why the frames carry
no RF path — the flood went out over the node's own internet uplink.

**The defect in the firmware.** `sendPosition()` has three trigger classes, and only two of them
are rate-limited:

| Trigger                                                        | `uintervall`  | Rate limit before this fix                                    |
| -------------------------------------------------------------- | ------------- | ------------------------------------------------------------- |
| Periodic beacon (`esp32_main.cpp:3221`, `nrf52_main.cpp:1859`) | interval      | yes — `posinfo_timer` plus a 30 s floor (`posinfo_timer_min`) |
| Track / WX (`--sendtrack`, `--sendweather`)                    | 0xEEEE/0xFFFF | yes — SmartBeaconing, minimum 10 s                            |
| **Shot: `--sendpos`, user button, EXTUDP telemetry injection** | 0x9999        | **none at all**                                               |

The shot path fires exactly once per trigger, however often the trigger comes. The most exposed
one is `handleExternTelemetry()` (`extudp_functions.cpp:211`): it beacons on **every accepted
`{"type":"tele"}` datagram** on the EXTUDP port — an unauthenticated LAN interface. A sender at
loop rate makes the node beacon at loop rate; an app or script looping `--sendpos` does the same.
Identical in `upstream/dev` (verified `2cb6bb4d`), so this is not a fork regression.

**Fix (this branch, 2026-08-30):** `src/beacon_rate.h` — `beaconShotAllowed()`, a pure,
rollover-safe helper — plus a guard in `sendPosition()` that drops a shot beacon when the last
own beacon is less than `BEACON_SHOT_MIN_MS` (30 s, the same floor the periodic path already
keeps) ago, logging `[POS];shot;suppressed;since_ms;…`. The periodic and track paths are
untouched. Native regression `test_beacon_rate` (6 cases: first shot, 400-trigger flood, exact
boundary, regular cadence, `millis()` rollover, parameterised floor) — fails without the helper,
green with it; `pio test -e native` 76/76; ESP32 and nRF52 targets build.

**Proven on hardware** (DK5EN-93, Heltec V3, flashed with this build 2026-08-30): three
`--sendpos` in a row → the first beacons, the second prints
`[POS];shot;suppressed;since_ms;89;min_ms;30000`, the third is dropped silently; after a 34 s
pause the next `--sendpos` prints `[POS];shot;resumed;suppressed;1` and beacons again. Both
markers are raw `Serial.printf` (visible with `--debug off`, `;` not filtered), and only the
**first** suppression per blocked window is printed — under a 20/s trigger storm the guard must
not turn into a 20 lines/s log storm (TM-21's lesson).

**Still open:** which trigger drove `DL6MDF-11` specifically is not decidable from the log — the
node is not ours. The fix bounds every trigger, so it does not depend on that answer. Worth doing
anyway: (a) the same floor question for `sendHey()` — 17 of the 18 field events are `hey`, and
`sendHey()` has no shot-path guard either, only the trickle timer; (b) EXTUDP is unauthenticated
and can drive node transmissions — that is a security property worth stating explicitly in
`08-defect-catalogue.md`; (c) hand the two corrections above back to the mcmap finding so its
"cause is upstream, not fixable from here" verdict gets revised.

#### PT-01 — protocol parsers and what tests them

Every place the firmware turns bytes from outside into structured data, and the suite that covers
it. "Hostile" means the tests go past the happy path: truncation, oversize, control bytes, NUL
inside a field.

| Parser / entry point                                                | Input from                     | Test suite                                                                                                                                | Hostile input?                                  |
| ------------------------------------------------------------------- | ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| `decodeAPRS()` / `encodeAPRS()`                                     | LoRa air, server UDP           | `test_aprs_decode`, `test_aprs_spec`, `test_aprs_corpus` (36 on-air frames + golden), `test_aprs_reencode`, `test_aprs_fuzz` (ASan/UBSan) | **yes** — truncation, discard rules, CRC corpus |
| `isPlausibleAckFrame()` (ACK)                                       | LoRa air                       | `test_ack_validate` (10), `test_ack_replay`, `test_aprs_fuzz`                                                                             | **yes** — field-measured split, fuzz corpus     |
| `updateHeyPath()` / `appendHeySignalReport()`                       | LoRa air                       | `test_hey_report` (8)                                                                                                                     | partly — chain length bound covered             |
| `is_new_packet()` (dedup ring)                                      | LoRa air, UDP                  | `test_dedup_replay` (trace replay)                                                                                                        | partly                                          |
| TX ring (`addTxRingEntry`, priority, overflow)                      | internal                       | `test_txring` (14), `test_txring_flood` (8), `test_txprio_replay`, `test_gwflood_frames`                                                  | **yes** — len 0 / len > buffer rejected         |
| External-radio bridge protocol                                      | TCP peer                       | `test_external_radio_protocol` (66), `_tcp` (37), `_txq` (23)                                                                             | **yes**                                         |
| BLE JSON frame builder                                              | internal → phone               | `test_ble_json_frame` (5)                                                                                                                 | partly — bound = buffer size (UP-01)            |
| Callsign / group validation (`checkRegexCall`)                      | LoRa air, commands             | `test_regex_call` (11)                                                                                                                    | partly — no length bound (UP-06, open)          |
| Settings load plausibility (`sanitize_*`)                           | NVS / flash                    | `test_settings_sanitize` (10)                                                                                                             | **yes** — corrupt struct, missing terminator    |
| NTP reply                                                           | network                        | `test_ntp_async` (10)                                                                                                                     | **yes**                                         |
| Capture ring, `printfdeb` format, `mask_secret`                     | internal                       | `test_capture_ring` (10), `test_printfdeb_format` (13), `test_mask_secret` (6)                                                            | **yes**                                         |
| **`decodeAPRSPOS()`** — position payload fields                     | LoRa air                       | **none**                                                                                                                                  | —                                               |
| **`decodeMHeard()`** — mheard binary record                         | flash / BLE                    | **none**                                                                                                                                  | —                                               |
| **`getExtern()` / `handleExternTelemetry()`** — EXTUDP JSON         | **unauthenticated LAN**        | **none**                                                                                                                                  | —                                               |
| **Serial/BLE command parser** (`commandCheck`, `sscanf` chains)     | USB, BLE, net console, web GUI | **none**                                                                                                                                  | —                                               |
| **`decodeTinyXML()`** — soft-serial measuring station               | UART                           | **none**                                                                                                                                  | —                                               |
| **`checkVia()` / via chain**                                        | LoRa air                       | **none** (Wave 0.6 lists it)                                                                                                              | —                                               |
| **UDP server frame indicators** (`GATE`/`BEAT`/`CONF`, `MAX_ZEROS`) | server                         | **none** directly (mock server exercises it end-to-end)                                                                                   | —                                               |
| **Phone → node command frames** (`phone_commands.cpp:248+`)         | BLE                            | **none**                                                                                                                                  | —                                               |
| **Web request / query parser** (`decodeURLPercentCoding`)           | HTTP                           | **none**                                                                                                                                  | —                                               |

Order of work proposed (by exposure, not by effort): `getExtern()` and the phone→node command
frames first — both are reachable without authentication from outside the node — then
`decodeAPRSPOS()` and the UDP indicator path (RF/server reachable), then the command parser, then
`decodeMHeard()`, `checkVia()` and TinyXML.

**Test bar for each of them** (operator, 2026-08-30): not only the happy path. Every field gets a
case with (a) a payload longer than the field, (b) the field truncated mid-way, (c) ASCII control
characters inside the field, and (d) **an embedded NUL** — the C string terminator is the one byte
that changes the meaning of every `strlen`/`snprintf`/`String` downstream, and the parsers here mix
`String`, raw buffers and `sscanf` freely. `test_aprs_fuzz` already runs under ASan/UBSan and is
the right pattern to copy.

**PT-01 findings (2026-08-30). ALL EIGHT FIXED later the same day** -- each pinned case is a real
assertion now (suites `native_parsers` 24/24, `native_extern` 32/32, `native_xml` 11/11, 0 skips);
finding 6 additionally rejects a NUL in `dst`. The table stays as the record of what was wrong:

| #   | Parser            | Finding                                                                                                                                                    |
| --- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | `decodeAPRSPOS()` | the `ipt>10` bail-out takes whatever byte sits at the cut-off as hemisphere/group byte without checking for `N`/`S` -- fabricated hemisphere, 11-digit lat |
| 2   | `decodeMHeard()`  | case 3: `mh_payload_type` is overwritten on every scanned byte, a record truncated after the type byte loses it to the zero padding                        |
| 3   | `decodeMHeard()`  | case 1: `mh_date` is built with `concat()` and no closing-delimiter fallback -- a record without `\|` absorbs the whole 55-byte window incl. NUL padding   |
| 4   | `getExtern()`     | the literal `"none"` doubles as the internal "no payload" sentinel; a message whose text is exactly `none` is silently dropped                             |
| 5   | `getExtern()`     | `dst` (<=9) and `msg` (<=150) are checked separately but `snprintf(val,160,":{%s}%s")` needs 162 at both maxima -- the last 3 characters vanish silently   |
| 6   | `getExtern()`     | an embedded `\u0000` in `msg` truncates the C-string pipeline (`strlen`, `String`, `snprintf`); the frame ships shortened, nothing signals it              |
| 7   | `decodeTinyXML()` | a `<VT>` without numeric text leaves `QueryFloatText()`'s output untouched: an uninitialised stack float is formatted into `node_values` and relayed       |
| 8   | `decodeTinyXML()` | `node_utcoff` is hard-coded `0.0` (`tinyxml_functions.cpp:~245`, the conversion is commented out) while `strTELE_UTCOFF` is filled -- dead timezone path   |

Also documented, not defects: `decodeTinyXML()` only ever uses the **last** `<VT>` per `<Values>`
(loop starts at `LastChildElement`), and `checkVia()`'s gateway/mheard branches are commented out
("22.07.2026 - zum Test entfernt").

#### WF-01 — trailing bytes on the wire

Three sites, verified by reading 2026-08-30:

1. **BLE notifications carry 1–3 bytes past the payload.** Both senders transmit `blelen + 2`
   bytes (`phone_commands.cpp:125` and `:199`) with the author's own comment
   `// why do we need to add 2 bytes??`, but the branches copy different amounts: text/position
   writes `1 + blelen` → **1** trailing byte; the `0x44` JSON branch writes `blelen` → **2**; the
   legacy `0x91` branch writes `blelen - 1` → **3**. The buffers are zero-initialised, so today
   those bytes read as `0x00`. Already described from the consumer side in
   [`11-wire-format.md` §4.3](architecture/11-wire-format.md).
2. **The UDP `KEEP` frame includes its string terminator** — `hb_buffer_size = strlen(keep_buffer)+1`
   (`udp_functions.cpp:1599`), while the sibling `DATA` frame uses plain `strlen()`
   (`:1635`). The format comment above it documents the `0x00` as intended, so this is a wire
   fact, not obviously a slip — but the asymmetry between the two frame types is real.
3. **`addUdpOutBuffer()` copies `len + 1` bytes** into the ring (`udp_functions.cpp:1565`) while
   only `len` are transmitted: a one-byte over-read of the caller's buffer. In-bounds for every
   caller today (both pass buffers with ≥ 20 bytes of slack), so latent, not live.

**Operator decision 2026-08-30: only site (3) is changed.** (1) and (2) are the wire format that
phone apps, MCProxy, mc-chat softnodes and the server parse today. Dropping the pad is a one-line
change and a compatibility event — it needs a bench test against a real phone app (BLE pad) and
against the mock plus a real country server (`KEEP`) before it ships. Both stay documented here
and unchanged in the code. Site (3) is internal and now reads `memcpy(..., len)` instead of
`len + 1` (`udp_functions.cpp:1565`): no wire effect, the sender has always transmitted `len`
bytes.

---

### 3.8k Unconfigured-node traffic, TX back-pressure, HEY floor (intake 2026-08-30, third list)

| ID    | Type | Sev.   | Location                                                                                                                                                                                                                                                                                           | Item                                                                                                                                                                                                                                                                                                                               | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| ----- | ---- | ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RX-01 | GAP  | Medium | `lora_functions.cpp` (RX path after `decodeAPRS`), `udp_functions.cpp` (server → LoRa), `configuration_global.h:18`                                                                                                                                                                                | Discard every frame whose source callsign is still the factory default `XX0XXX` — unlicensed traffic, currently relayed across the whole mesh.                                                                                                                                                                                     | **DONE 2026-08-30** -- `isUnconfiguredCall()` guard after `decodeAPRS()` in `OnRxDone` and on the GATE-in path (`bUDPtoLoraSend`); `[RX];drop;unconfigured` marker (1 line/10 s); native predicate tests. Not exercisable on the bench any more (TX-01 stops the sender)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| TX-01 | GAP  | Medium | `lora_functions.cpp:1447` (`doTX()`), `txring_functions.cpp` `addTxRingEntry()`, `configuration_global.h:18`                                                                                                                                                                                       | **An unconfigured node must not transmit at all.** The other half of `RX-01`: stop the traffic at its source instead of only discarding it at every receiver.                                                                                                                                                                      | **DONE 2026-08-30** -- refused in `addTxRingEntry()` and in `doTX()`; `[TX];refuse;unconfigured` marker; verified on DK5EN-93 (`--setcall XX0XXX-00` + `--sendpos` -> refused, callsign restored)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| BP-01 | GAP  | High   | `loop_functions.cpp` `sendMessage()`, `txring_functions.cpp` `addTxRingEntry()`, `phone_commands.cpp`, `udp_functions.cpp`                                                                                                                                                                         | Tell the sender when the TX queue is filling, refuse politely at 80 %, and say so when a message is dropped — as Q-code notices back on the transport the message came from.                                                                                                                                                       | **DONE 2026-08-30** -- `src/backpressure.h` state machine (18 native cases), origin tag per transport (serial/BLE/web/EXTUDP/T-Deck GUI), `[BP];notice                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | refuse`markers; texts`QRS - slow down, TX buffer is filling`/`QRT - stopping to accept new messages, TX buffer full`/`QTA - message discarded, TX buffer full`/`QRV - ready again, TX buffer clear`; QRV only at depth 0 (depth 1 flapped on the bench). Verified on DK5EN-93: 22-message burst -> one QRS at depth 2, one QRT at 16/20, refusals, one QRV after the drain. **`onRefuse()`was dead code from 2026-08-30 until BP-07 (2026-09-01)**: it always returned`BP_NOTICE_NONE`because`refusing()` implies the latch already sits at QRT or higher, so a refused message never actually produced a first-time notice (L1, found in the BP-advisor review) |
| BP-02 | BUG  | High   | `txring_functions.cpp` `txRingDepth()`/`addTxRingEntry()`, `esp32_main.cpp`/`nrf52_main.cpp` (RING_STATUS/TX_GATE/TX_START), `lora_functions.cpp` (RING_TX_READ), `tools/serial_monitor.py`, `tools/loganalyse.sh`                                                                                 | Ring depth was the index distance `iWrite-iRead` and counted freed holes behind a priority-starved entry at iRead as still queued -- DJ8MEH-RCA 2026-08-31: 8-min QRT episode on phantom depth 19 vs. 3-4 real.                                                                                                                    | **DONE 2026-08-31** (`e501e63c`) -- depth = occupied-slot count, all report sites unified, `RING_STATUS` additionally carries `dist=` (old distance) and both RING_ZOMBIE detectors use it (queued fallback for pre-BP-02 logs); 2 fails-before tests in test_txring; Fable advisor gate passed (1 rework: RING_TX_READ)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| BP-03 | BUG  | Medium | `txring_functions.cpp` `txRingAgeBackground()`, `configuration_global.h` `RING_BG_MAX_AGE_MS`, both mains (2-s tick)                                                                                                                                                                               | A priority-starved BACKGROUND (HEY, prio 5) ring entry could sit forever (DJ8MEH blocker: 10 min) and pin the ring front; a >3-min-old neighbourhood report is worthless on air.                                                                                                                                                   | **DONE 2026-08-31** (`180917a1`) -- 2-s main-loop sweep drops BG entries older than 180 s (`RING_DROP_STALE` marker), atomically under the nRF52 lock (eviction-relocation race), EXT_PENDING exempt, rollover-safe; deliberately NOT in `getNextTxSlot()` (runs on the nRF52 timer task); 4 fails-before tests; advisor APPROVED. **Field-proven overnight 2026-09-01**: 13x `RING_DROP_STALE` on the live gateway, all at age_s 180-181 (`docs/wifi-soak-report-20260831.md`, Nachtrag)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| BP-04 | BUG  | Medium | `backpressure.h`, `loop_functions.cpp` (both `bp_state` call sites)                                                                                                                                                                                                                                | A QRT episode only ever closed at depth 0 -- a relay node with steady background traffic never reached 0 and kept refusing user messages long after the real backlog drained.                                                                                                                                                      | **DONE 2026-08-31** (`fdda7f2a`) -- depth 0 still closes immediately; the water band (depth 1) closes after a 10-s uninterrupted hold (explicit armed-bool sentinel, time injected via `poll/onSend(now_ms)`, header stays Arduino-free); 08.30 anti-flap pinned as regression test; 5 new native cases incl. uint32 rollover; advisor APPROVED                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| BP-07 | BUG  | High   | `src/backpressure.h`, `src/bp_notice_frame.h`, `src/loop_functions.cpp`, `src/extudp_functions.cpp`                                                                                                                                                                                                | L1: `onRefuse()` was provably dead code, constant `BP_NOTICE_NONE` (see the BP-01 row). L2: even a message the ring dropped after acceptance got no notice at all. A refused or dropped message left the sender with zero app-visible feedback, ever.                                                                              | **DONE 2026-09-01** (`77b43d4b`) -- new `BpNack` vocabulary in `backpressure.h`, deliberately never latched (separate from the latched episode `BpNotice`); `onRefuse()` now unconditionally returns `BP_NACK_QRT`; one frame per lost message, `QRT NOT SENT - <text>` / `QTA NOT SENT - <text>` (E1); `bpNackCompose()` truncates to 120 bytes on a UTF-8 codepoint boundary and sanitises `"`, `\` and control bytes before JSON escaping (an unsanitised text full of quotes could double in size on EXTUDP escaping and overflow the datagram); `bpDeliver()` split out of `bpEmitNotice()` so the new `bpEmitNack()` shares the transport switch; new `[BP];nack;` console marker, `txt;` field gated on `bLORADEBUG` (E6, keeps message content out of the always-on 2323 capture); `bpNextMsgId()` (E5) replaces raw `millis()` for every BP frame, because the QTA path emits two frames from one `sendMessage()` call that would otherwise collide on the same millisecond and lose one to the app's dedup filter; the refuse-check moved past `{ZIEL}` parsing so the nack carries the real decoded text, and `bpPeekDst()` (a throwaway second parse that existed only because the check used to run too early) is deleted (~60 lines); `sendExternNotice()`'s `c_json` grows 300 -> 400 bytes (N-22 BSS pattern) because the worst case left only 21 bytes of headroom. New integration case `test_flood_13_into_10_yields_five_nacks` (13 sends into a 10-slot ring: 8 accepted, 5 refused, 5 nacks -- 0 nacks before the fix). Gate 528 native / 11 envs, 6 board builds                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| BP-08 | BUG  | High   | `src/loop_functions.cpp`                                                                                                                                                                                                                                                                           | L3: the app echo for a locally typed message went out before `addTxRingEntry()` was even asked. A message the ring then dropped looked sent in the chat, and the QTA notice arrived afterward as an unrelated, unlinked line. On a gateway, the 0x41 "server reached" status even went out for a message that never left the node. | **DONE 2026-09-01** (`e8b82a02`) -- ring write moved ahead of the echo block; the drop branch (`w < 0`) now returns before any of it runs: no echo, no `insertOwnTx()`, no `addLoraRxBuffer()`, no `addNodeData()` uplink, no `sendExtern()` mirror, no console echo. Operator decision E4, bigger than L3: a gateway's UDP uplink used to run independently of the ring outcome -- a message the ring dropped still reached the central server without any HF neighbour ever hearing it. A message now enters the network whole or not at all; the sender gets `QTA NOT SENT - <text>` and knows to retry. Concurrency checked: `doTX()` runs from the main loop on both platforms (`esp32_main.cpp:2569`, `nrf52_main.cpp:1464`), `sendMessage()` is loop-context only (N-22) -- no interleaving window. Gate 528 native / 11 envs, 6 board builds; `sendMessage()` still `void` (return value in BP-09)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| BP-09 | GAP  | Medium | `src/backpressure.h`, `src/loop_functions.h/.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/nrf52/nrf52_ble.cpp`, `src/t-deck/event_functions.cpp`, `src/t-deck-pro/ui_deckpro.cpp`, `src/web_functions/web_functions.cpp`, `src/extudp_functions.cpp`, `test/test_getextern/` | L4: `sendMessage()` was `void`, so no caller could react to the outcome. On the T-Deck, `event_functions.cpp` cleared the input field and switched to the message list unconditionally after the call -- on a refusal the typed text was gone from both the field and the list.                                                    | **DONE 2026-09-01** (`4f97f7f0`) -- `sendMessage()` returns `int` (`BpSendResult`: 0 accepted, -1 refused/QRT, -2 dropped/QTA, -3 invalid) across all four signatures; three of eight call sites act on it -- T-Deck `event_functions.cpp` (clear field + tab switch only on `BP_SEND_OK`), T-Deck Pro `ui_deckpro.cpp` (same), `web_functions.cpp` (`sendmessage refused/dropped/invalid` instead of a blanket `ok`); the other five get `(void)`, their feedback already travels back as a nack over the transport. Beyond the original plan: the web GUI's own JS cleared both input fields right after `xhttp.send()` without ever reading the response -- the same text loss one layer up, on a third surface; clearing now hangs off `onreadystatechange` and fires only on `sendmessage ok`. Gate 528 native / 11 envs, 6 board builds                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| BP-10 | BUG  | High   | `src/bp_notice_frame.h`, `src/loop_functions.cpp`, `src/t-deck-pro/ui_deckpro.cpp`, `src/t-deck/event_functions.cpp`, `src/web_functions/web_functions.cpp`, `test/test_bp_notice_frame/`                                                                                                          | Independent Fable advisor round over the whole BP-07/08/09 diff (`458af2b1..4f97f7f0`), 7 finders, every finding re-verified by the orchestrator at the source. Three real bugs, all caused or amplified by the three preceding waves.                                                                                             | **DONE 2026-09-01** (`3aecc90f`) -- H1: BP-09's `BP_SEND_OK` gating hid the BP-07 nack on both T-Decks (the tab switch / `scr_mgr_switch()` never happened, so a refused message gave zero visible feedback -- worse than before BP-07); now unconditional again, only the input-field clear stays gated. H2: `addTxRingEntry()`'s single -1 return code was read as backpressure for all three of its causes, so a factory-fresh node forced a false `QRT`/"TX buffer full" cycle on an empty ring for every typed message; the drop branch now checks ring depth against `bp_state.refuseThreshold()`, below it a new `BP_SEND_INVALID` and a `[BP];invalid;depth;N;max;M;ms;T` line, no state transition, no nack. H3: the `[BP];nack;` marker logged the raw message text instead of the sanitised one, so an embedded LF or a fake `[MC-DBG]` marker inside a user text could corrupt the console line that `tools/serial_monitor.py`/`loganalyse.sh` parse; `bpNackCompose()` now runs before the log line. Plus five medium fixes: M1 `body[140]` moved to BSS on nRF52 (was on the 4 KB loop stack with a measured watermark of 0), M2 truncation now uses the existing `charset_utf8_safe_truncate()` instead of a hand-rolled reverse scan that could delete a whole text on stray continuation bytes, M3 the ellipsis is now decided after the buffer clamp, M4 `bpEmitNack()` now respects the episode latch, M5 `bp_episode_origin` is reset before sending instead of after (re-entrancy via `addMessage()`'s 100 ms `lv_task_handler()` spin), M7 the `bpPeekDst` empty-`{}` -> `*` rule is restored for `bp_origin_dst`, M8 `bpNextMsgId()` skips a 0 result on the 49.7-day rollover; plus the same web-JS input-clearing fix carried over from BP-09. Captured, not fixed: `addMessage()`'s 100 ms spin and `TDeck_pro_lora_disp()`'s `delay(100)` now run per refused message instead of ~twice per episode (M6, out of scope, a later bench run should show the real magnitude). H2/H3/M1/M4/M5/M7 are not natively testable -- `loop_functions.cpp` is in no native `build_src_filter`; the bench run is the only end-to-end proof. Gate 530 native / 11 envs, 6 board builds |
| FL-02 | BUG  | High   | `loop_functions.cpp` `sendHey()`, `command_functions.cpp:3285`                                                                                                                                                                                                                                     | `sendHey()` has the same missing floor that `FL-01` closed for `sendPosition()`: the trickle path is bounded, the `--sendhey` command path is not.                                                                                                                                                                                 | **DONE 2026-08-30** -- `sendHeyShot()` with its own timestamp, `--sendhey` routed through it, trickle untouched; `[HEY];shot;suppressed` verified on Heltec and RAK                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

#### RX-01 — frames from unconfigured nodes

Observed 2026-08-30 on the T-Deck's mheard/path view: `XX0XXX-00`, heard 16:21, path
`4/DB0HOB-12,DL2JA-2,DK5EN-98` — a node that has never been configured is transmitting, and three
relays carried it. A node with the factory callsign is by definition not identifying itself, so
the traffic is not legal to relay in the first place; today the whole mesh forwards it, uploads it
to the central server, and puts it in every neighbour's mheard list.

`isNodeUnconfigured()` (`configuration_global.h:18`, ALT-34) is already the single place that
answers "is this the factory default", covering `XX0XXX*`, empty and `none`. The work:

- **Drop point:** in the RX path right after `decodeAPRS()` fills `aprsmsg.msg_source_call`, i.e.
  **before** mheard, display, phone/BLE out, the gateway upload and the relay decision. One place,
  shared by every board — `lora_functions.cpp` is common code, so "across all hardware platforms"
  needs exactly one guard, not one per platform.
- **Second door:** the server → LoRa path (`udp_functions.cpp`, `GATE` frames) must not radiate
  such a frame either.
- **Log:** one line per dropped frame **only** when serial debug is on, e.g.
  `[RX];drop;unconfigured;call;XX0XXX-00;id;<msg_id>`, as a raw `Serial.printf` (`printfdeb`
  filters `;` and is silent with `--debug off`, see the FL-01 marker note).
- **Test:** `isNodeUnconfigured()` is a pure predicate and trivially unit-testable; the drop point
  needs a decode-level case in the `test_aprs_*` family (frame with `XX0XXX-00` source → dropped,
  same frame with a real callsign → passes) plus one bench check with the frame injector.

**Both side questions are decided (operator, 2026-08-30).** (a) "Should such a node refuse to
transmit at all" — yes, and it is **now its own item, `TX-01` below**. (b) "The node then no
longer appears on the map" — **that is wanted, not a cost.** The callsign is not a legal
identification in the first place, and every unconfigured node in the world carries the _same_
one: on the map and in the database they do not appear as several nodes but collapse into one
`XX0XXX-00` whose position, hardware and firmware jump between whichever of them was heard last.
Suppressing them removes a phantom station, it does not hide a real one.

Consequence to write into the bring-up documentation: "did my new node come up?" is answered
**locally** — WiFi-AP mode, the web GUI, `--info` over USB — never by looking for it on the map.
A node that is visible on the map has, by definition, already been given a callsign.

#### TX-01 — an unconfigured node refuses to transmit

`RX-01` keeps `XX0XXX` traffic from spreading; this one keeps it from existing. A node that has
not been given a callsign is not identifying itself, so nothing it sends is legal to put on the
air — and the node is the only place where that can be settled at the root. With both items in
place the class is closed at both ends; either one alone is a partial fix.

**Where the guard goes.** Two choke points, and the recommendation is to use both:

- `doTX()` (`lora_functions.cpp:1447`) is the **only** function in the tree that calls
  `Radio.Send()` (three branches inside it). A check there is the hard backstop — nothing reaches
  the antenna, on every board, from one place. It is also the natural home: `doTX()` already
  carries a `TX_ENABLE` compile-time gate, and this is its runtime sibling.
- `addTxRingEntry()` in addition, so an unconfigured node does not even fill its ring. The `-1`
  return path already exists and is exactly the signal `BP-01` will consume, so the phone can be
  told **why** a message was refused instead of watching it disappear.
- Note the `EXTERNAL_RADIO` build sends through the TCP bridge, not `Radio.Send()` — a
  `doTX()`-only guard would miss it, the `addTxRingEntry()` guard would not.

**What must keep working while the radio is silent:** everything needed to get the node
configured. WiFi-AP provisioning already starts by itself in this state
(`esp32_main.cpp:879`), and USB serial, the web GUI and BLE are untouched. Only the transmitter
is off.

**Make it visible, or it becomes a support case.** "My new node does nothing" is the predictable
consequence, so: one line at boot, a line in `--info`, a hint on the display, and — when a send
is actually attempted — one notice per state change rather than one per frame (TM-21's lesson,
and the same channel `BP-01` builds).

**Bench handle for the test:** `checkRegexCall()` accepts `XX0XXX-00` (the pattern matches it), so
`--setcall XX0XXX-00` puts a bench node into the unconfigured state and a real callsign takes it
back out — no NVS erase needed. Native: the predicate plus the guard's decision function. Bench:
set the default callsign, then `--sendpos` and `--sendhey` must produce no TX marker and one
notice; restore the callsign and TX must resume. Run it on all four boards, since the guard sits
in shared code but `doTX()` has board-specific branches.

**To decide when this is built:** does production/factory testing need a deliberate way to
transmit while unconfigured (e.g. a command that arms TX for a few minutes)? If yes, it must be
local-only — never reachable from the air or from the server.

#### BP-01 — back-pressure to the sender, in Q-codes

The queue state must reach the person who is typing, on **the transport the message came from**
(BLE/phone, UDP, serial, web GUI) — never over the air, because a notice that is radiated adds to
the very congestion it reports. Three states, with the matching Q-codes
([Q-code list](https://en.wikipedia.org/wiki/Q_code)):

| State                         | Q-code  | Standard meaning                         | Text                                           | Node behaviour                             |
| ----------------------------- | ------- | ---------------------------------------- | ---------------------------------------------- | ------------------------------------------ |
| Queue depth > 1               | **QRS** | "transmit more slowly"                   | `slow down, TX buffer is filling`              | still accepts                              |
| Queue ≥ 80 % of `MAX_RING`    | **QRT** | "stop transmission"                      | `stopping to accept new messages, buffer full` | **refuses** new user messages              |
| `addTxRingEntry()` dropped it | **QTA** | "cancel nr … as if it had not been sent" | `messages discarded, TX buffer full`           | message is gone, sender is told            |
| Queue drained again           | **QRV** | "I am ready"                             | `ready again, TX buffer clear`                 | accepts again — **only after** QRS/QRT/QTA |

**Scope of the refusal (operator, 2026-08-30): only locally originated user messages.** Relay
traffic, ACKs and beacons keep flowing — the node stays a working relay for the network, only the
flooding user is throttled. That also keeps the refusal out of the RF-reachable paths, so nobody
outside can push a node into "not accepting" from the air.

Design points that follow from the code:

- Today `sendMessage()` ignores `addTxRingEntry()`'s return value entirely (that is TM-37); the
  return already distinguishes "queued in slot n" from `-1` (`RING_DROP_NEW`). The depth is
  computed inside `addTxRingEntry()` (`queued`), and `stat_queue_hwm` already tracks the high-water
  mark — so all three thresholds are readable where the decision is made.
- **`MAX_RING` is not the same on every board** — 20 on Heltec/T-Deck class, 10 on one, 30 on
  another (`configuration_global.h:174-200`). 80 % must be computed from `MAX_RING`, never
  hardcoded, or the T-Deck warns at a different fill level than the RAK.
- **One notice per state transition, not per message** (TM-21's lesson): a QRS for every message
  of a burst is itself a flood. Hysteresis is needed, e.g. re-arm QRS only after the queue has
  fallen back below the threshold.
- **QRV when it is over (operator, 2026-08-30): accepted, but conditional.** A fourth notice
  **QRV** ("I am ready") goes out once the queue has cleared — **but only if at least one of QRS,
  QRT or QTA was actually sent in this episode.** A node that was never under pressure never
  announces that it is ready; QRV is the closing bracket of a warning, not a heartbeat.
  Concretely: latch the highest notice sent (none → QRS → QRT → QTA), fire QRV exactly once when
  the depth falls back into the quiet band, then clear the latch. One QRV per episode, on the same
  transport the warnings went to — and if that transport is gone (phone disconnected meanwhile),
  skip it rather than queue it.
- The notice must carry the same `msg_id` semantics as any other message the app displays, so the
  phone shows it in order; it must not be counted as a user message for retransmission.

**Regression tests, on all four platforms** (explicit operator requirement):

- Native: extend `test_txring_flood` (it already pins the ring's overflow policy) with the three
  thresholds, the hysteresis, and the per-board `MAX_RING` arithmetic. Cheap and deterministic.
- Bench: `tools/bench/experiments/gwflood.py` already produces a message burst; it needs to assert
  the three notices come back in order on the same transport, on T-Deck Plus, Heltec V3, T-Beam and
  RAK4631. The RAK is the interesting one — different `MAX_RING`, and its queue drains through the
  W5100S gateway path (TM-35).

#### FL-02 — the same floor for `sendHey()`

`FL-01` closed the shot path of `sendPosition()`. `sendHey()` still has the identical hole: the
periodic trickle path is bounded (`TRICKLE_IMIN_S` = 30 s), but `--sendhey`
(`command_functions.cpp:3285`) calls `sendHey()` directly with no spacing check at all. This is not
academic — **17 of the 18 field events in the mcmap finding are `hey`, only one is `pos`**, so the
HEY path is the one that actually fired in the field.

Fix: the same `beaconShotAllowed()` helper with its own last-sent timestamp and the same
`BEACON_SHOT_MIN_MS`, applied to the command path only; the trickle scheduler keeps its own
cadence and must not be double-gated. Extend `test_beacon_rate` with the HEY timestamp, and use
the same bench proof (two `--sendhey` inside 30 s → the second prints the suppression marker).

### 3.8l EXTUDP on the RAK4631 — reported crash, and the missing UDP regression test (intake 2026-08-30, fourth list)

| ID     | Board(s)                      | Type | Sev. | Location                                                                                                                            | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ------ | ----------------------------- | ---- | ---- | ----------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| UDP-01 | RAK4631 (nRF52840 + W5100S)   | BUG  | High | `src/extudp_functions.cpp`, `src/nrf52/nrf52_main.cpp:1086,2420,2429,2461`, `src/loop_functions.cpp` `sendMessage()`                | **Second-hand report ("angeblich"): switching `--extudp on` on a RAK kills the node.** Two defects with exactly this symptom were found on this bench and fixed — `N-22` (loop-task stack overflow) and `N-23` (brick trap without Ethernet) — and **both fixes are also in `upstream/dev`**. So this is an old build, a third cause, or the silent-dead case below. Not reproduced yet.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | **answered in part 2026-08-30 (TM-43 run)** -- the inbound path measured: EXTUDP rx leaves 424 B of the 4 kB loop stack, the gateway UDP->LoRa path 276 B (run minimum) -- thin, but not N-22's zero. The reporter questions (version, trigger, crash vs freeze) still stand; UDP-02 below is a real candidate for the "silently dead" flavour and is fixed                                                                                                                                                                                                                                                                                             |
| TM-43  | RAK4631, Heltec V3 as control | TEST | High | `tools/bench/rak_harness.py` (new `extudp` scenario + UDP peer helper), `test/test_getextern/`, env `native_extern`                 | **Regression test that drives the RAK's UDP interface in both directions — send and receive (operator request 2026-08-30).** The EXTUDP transport has never had a hardware test; every EXTUDP defect so far was found by hand at the bench. The test must assert the node is still alive at the end, not just that datagrams flew.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | **DONE 2026-08-30** -- `extudp` scenario in `rak_harness.py` + `tools/bench/extudp_peer.py` (24 unit tests), `[EXT];rx/tx;stack_hwm` instrument, `test_getextern` 32 cases. RAK DK5EN-90: PASS, 601 s soak, 197/197 datagrams picked up, 0 resets. Heltec control: first run FAILED and found UDP-02; PASS after the fix. `docs/bench-extudp-regression.md`                                                                                                                                                                                                                                                                                             |
| TM-44  | RAK4631                       | BUG  | Low  | `src/nrf52/nrf_eth.cpp` `udpUpdateTimeClient()` (`EthStall("ntp")`, ~:1185), `src/ntp_async.cpp` send path, W5100S RTR/RCR defaults | **1.6 s loop-task block at the `ntp` site while the WAN is dead — first real catch of TM-35's recurring-stall marker** (`[ETH];stall;ntp;ms;1609`, TM-38 run `apreboot_ap1_20260830-220423`, t0+65 s: LAN link already back, router still rebooting). Not DNS — this path uses a literal IP (`nrf_eth.cpp:1159`). Prime suspect: the W5100S's **on-chip retry schedule blocking the UDP send while the IP default gateway (the Orbi router, 192.168.68.1 -- NOT a MeshCom gateway) does not answer ARP**; note this blocks EVERY off-LAN UDP send equally (KEEP via `sendUDP()` runs the same chip path) -- NTP was merely the call wrapped in a stall marker — chip defaults RTR 200 ms x RCR 8 = 1600 ms, matching the measured 1609 ms almost exactly; `nrf_eth.cpp` never configures RTR/RCR. Candidate fixes, in order: skip the NTP request while the gateway is unreachable (cheap gate), or lower RTR/RCR for bounded sends (affects every socket — needs care), or accept (1.6 s once per WAN outage, self-recovering). Reproduction without killing the bench Mac's session: detached capture (`meshlogger.py` or a `serial_session` redirect) on the RAK, then pull the **router uplink** (not the RAK's LAN cable — that tests the link-down path, which is gated and never sends) for ~2 min around a 15-min NTP refresh, reconnect, analyse offline. | **DEFERRED (operator, 2026-08-30)** — the gate is NOT wanted: a "router unreachable" heuristic built on stale heartbeats or fresh DHCP/link edges creates its own failure modes — a central-server outage makes `hb_age` stale while the router is fine (the node would wrongly stop sending), a routine DHCP renew or link flap would wrongly suppress sends, and every such gate needs its own re-arm logic. Too many side issues for a 1.6 s, once-per-WAN-outage, self-recovering block. Leave the W5100S retry schedule alone too (RTR/RCR changes affect every socket). Revisit only if the stall marker shows it firing outside real WAN outages |
| UDP-02 | ESP32 WiFi nodes              | BUG  | High | `src/extudp_functions.cpp` `getExternUDP()`                                                                                         | **One 255-byte datagram permanently killed EXTUDP receive on ESP32** (found by the first TM-43 control run on DK5EN-93): `getExternUDP()` read only 254 of 255 bytes, arduino-esp32 `WiFiUdp::parsePacket()` returns 0 while an unread rx_buffer exists -- every later datagram silently ignored until reboot, sending kept working. RAK/`EthernetUDP` unaffected (its `parsePacket()` discards remainders itself). Fix: drain the remainder (`UdpExtern.flush()`, verified to free the buffer on both installed cores) + one `[EXT] oversized datagram drained` line.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | **FIXED 2026-08-30** -- Heltec `extudp` scenario before 40 in / 0 picked up, after 40/40; analysis in `bench-extudp-regression.md` §6                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |

#### UDP-01 — what the tree already knows about this symptom

Twice on this bench, and both times real:

- **`N-22` (2026-08-21, fixed `9ce62aa0`)** — RAK gateway with `--extudp on` reboots reproducibly
  2–4 s after every message it sends itself, `RESETREAS=0x00000004` (app crash through the
  SoftDevice fault handler). Root cause **measured**, not guessed: `uxTaskGetStackHighWaterMark(NULL)
== 0` at the deepest point of `checkSerialCommand()` → `sendMessage()` → `sendExtern()`. The nRF52
  loop task has a hard-coded 4 KB stack (`LOOP_STACK_SZ`, Adafruit core); EXTUDP was only the
  trigger that pushed the path over the edge. Fix: the path's large buffers into BSS.
- **`N-23` (2026-08-22, fixed `b62976c9`)** — `--extudp on` with gateway **and** webserver off bricks
  the node permanently. Setup initialises the W5100S only under `bGATEWAY || bWEBSERVER`
  (`nrf52_main.cpp:1086`), while the 15-minute restart block runs under `bWEBSERVER || bEXTUDP` and
  fires on the first pass (`web_timer == 0`); `startExternUDP()` then hit uninitialised hardware and
  never returned — loop task dead, console without echo, and because the flag is persisted, the same
  after every reboot. Fix: both restarts gated on `neth.hasIPaddress`.

**Both fixes are in `upstream/dev` as well** (checked 2026-08-30: the `#ifdef ESP32 / #else static
char c_json[500]` block in `sendExtern()`, and the `if(bEXTUDP && neth.hasIPaddress)` gate in the
loop restart block). "Upstream simply doesn't have the fix" is therefore ruled out, and nothing
should be changed in the code before these four answers are in:

1. **Firmware version and board** of the reporting node — the two fixes above are what separates a
   current build from a mid-August one.
2. **When** it dies: on `--extudp on` itself, on the first message the node sends, on the first
   inbound datagram, or minutes later while idle. Those are three different suspects.
3. **Crash or freeze:** the boot line `[BOOT] RESETREAS=` (`0x4` app crash, `0x2` watchdog, `0x8`
   lockup) versus a console that stops echoing without a reboot — the latter is `N-23` class, a
   blocked W5100S socket call, not a reset.
4. **`--gateway` / `--webserver` state and whether `--extudpip` is set** — see the two open points
   below, both depend on it.

Two things on this branch still fit the report:

- **The inbound path has never been stack-measured.** `N-22` measured the outbound path only. Inbound
  is deeper: `getExternUDP()` (`extudp_functions.cpp:322`) → `getExtern()` (`:238`, `char val[161]`
  plus an ArduinoJson `JsonDocument` on the stack) → `sendMessage()` → `sendExtern()`, i.e. the whole
  `N-22` path with the JSON decoder stacked on top, called from the same 4 KB loop task
  (`nrf52_main.cpp:2423`). One watermark print at that depth settles it the same way it settled
  `N-22`. This is also why the operator's "send **and** receive" framing is the right one: the
  receive direction is the untested half and the deeper one.
- **EXTUDP on its own is silently dead, not crashed.** With gateway and webserver off the W5100S is
  never initialised, so `startExternUDP()` never runs, `hasExternIPaddress` stays `false`, and both
  `getExternUDP()` (`:329`) and the send path (`:603`) return early — no traffic in either direction
  and no error message. Since the `N-23` fix that is safe, but invisible: a user who switches EXTUDP
  on, sees nothing happen and reboots would describe it as "broken" too. Whether the setup gate at
  `nrf52_main.cpp:1086` should become `bGATEWAY || bWEBSERVER || bEXTUDP` is an **open decision, not
  a task** — it re-opens exactly the door `N-23` closed and must not be changed without the
  hardware-present check and TM-43 in place.

#### TM-43 — the regression test over the RAK's UDP interface

Both directions, on real hardware, with **liveness as the actual assertion** — datagrams arriving is
the easy half; the failure under investigation is the node dying while they do. Node: `DK5EN-90`,
`/dev/cu.usbmodem201301`, env `wiscore_rak4631`, W5100S with link (bench fleet table in §3.8f).

**Send direction (node → host, UDP :1799, `EXTERN_PORT` in `configuration_global.h:160`)**

- Host binds UDP 1799 and configures the node over serial: `--extudpip <host-ip>`, `--extudp on`,
  with `--gateway on` so Ethernet is actually up (the `N-23` order trap: gateway first).
- Triggers, one at a time with the console captured: `--sendpos`, an own message
  (`::{TEST}...`, group `TEST` per TM-42), and one frame received over LoRa from a second node.
- Assert per trigger: a well-formed JSON datagram arrives at the host (`type`, `src_type` = `node` /
  `lora`, payload matching what went out), and the console shows the matching `[EXT] Out:` line.
  A trigger that logs `[EXT] Out:` but produces no datagram is the `N-22` side symptom and must fail
  the run.

**Receive direction (host → node)**

- `{"type":"msg","dst":"TEST","msg":"..."}` → the node builds `:{TEST}...` and puts it in the TX ring
  (`--debug on` marker plus the LoRa frame at a second node); this is the deep path from the UDP-01
  notes and the one most likely to crash.
- `{"type":"tele","temp":23.3,"hum":60,"press":1018.5}` → the values land in
  `meshcom_settings.node_*` and the immediate beacon carries them (`handleExternTelemetry`,
  `extudp_functions.cpp:167`). Only meaningful on a node without real sensor hardware — the function
  refuses otherwise, and that refusal is itself a case.
- Rejection cases, all of which must log and survive: broken JSON, missing `dst`/`msg`, `dst` longer
  than 9, `msg` longer than 150, a datagram of the full `UDP_TX_BUF_SIZE` (255 B) and one truncated
  mid-JSON.

**Liveness — the part that catches the reported bug**

- Green only if the node is still up at the end: uptime monotonically increasing across the run, no
  `[BOOT] RESETREAS=` line after the start, no `HardFault` / assert (`rak_harness.py` already carries
  the `CRASH` regex), and the console still echoes after the last case.
- Soak tail: EXTUDP on with traffic in both directions for ≥ 10 minutes, then the same liveness
  check. `N-22` crashed 2–4 s after a send, `N-23` on the first loop pass — 10 minutes covers both
  classes with margin.
- With `MC_TEST_HOOKS` the 500 ms sequence-numbered heartbeat (`extudp_functions.cpp:337`) gives the
  host a gap-free clock: a gap in `seq` dates the stall to the millisecond and separates "network
  gone, loop alive" from "loop task hung" — the instrument that settled `N-20`.

**Where it goes**

- New `extudp` scenario in `tools/bench/rak_harness.py` (holds the port, boot phases and crash regex
  already), plus a small UDP peer helper beside it; runs land in `tools/bench/runs/` like every other
  run.
- Cheap half first, natively: `test/test_getextern` (`pio test -e native_extern`) already covers
  `getExtern()` parsing — extend it with the rejection vectors above so hardware time is spent only
  on transport and stack.
- Run the same probe against a Heltec V3 as the **control**. `extudp_functions.cpp` is shared code;
  if the sequence survives on ESP32 and kills the RAK, that is the 4 KB loop-task stack again and not
  an EXTUDP logic bug — the exact discrimination that cost `N-22` several wrong turns.

### 3.8m Memory budget — static-DRAM guard and the heap-ring proposal (2026-08-30)

| ID     | Board(s)        | Type | Sev.   | Location                                                                                                                   | Item                                                                                                                                                                                                                                      | Status                                                                                    |
| ------ | --------------- | ---- | ------ | -------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| MEM-01 | classic ESP32   | GAP  | High   | `tools/resource_watch.py`, `.github/workflows/ci-build.yml`, `configuration_global.h`                                      | Static-DRAM guard rail (`dram` subcommand, hard CI gate at 4 kB headroom) and classic-ESP32 rings right-sized (`MAX_RING` 30→20, `MAX_RING_UDP` 25→20).                                                                                   | **DONE 2026-08-30**, commit `861f2967` — headroom E22 1,712→11,896 B, T-Beam 528→10,712 B |
| MEM-02 | all (see risks) | GAP  | Medium | `src/loop_functions.cpp:403-435`, `src/loop_functions_extern.h:189-237`, `src/txring_functions.cpp:27` (NATIVE_BUILD twin) | **Move the five big static rings to one-time boot allocation** — frees ~28 kB of `dram0_0_seg` on classic ESP32. **Parked: risk assessment before any code (operator, 2026-08-30), and it ships only with the regression bar below met.** | **parked — risk assessment first**                                                        |

#### MEM-02 — what would move, and the risks that must be assessed first

The five rings, all defined in `loop_functions.cpp:403-435`, all `.bss` today
(sizes at the post-MEM-01 classic-ESP32 dimensions; `UDP_TX_BUF_SIZE` 255,
`MAX_MSG_LEN_PHONE` 300):

| Ring                  | Dimensions           | Bytes | Written from                                                                 |
| --------------------- | -------------------- | ----- | ---------------------------------------------------------------------------- |
| `BLEtoPhoneBuff`      | `MAX_RING × 305`     | 6,100 | loop task                                                                    |
| `BLEComToPhoneBuff`   | `MAX_RING × 305`     | 6,100 | loop task                                                                    |
| `ringBuffer` (TX)     | `MAX_RING × 260`     | 5,200 | loop task + `OnRxDone` relay path                                            |
| `ringBufferUDPout`    | `MAX_RING_UDP × 275` | 5,500 | **reachable from `OnRxDone` via `addNodeData()`** (`udp_functions.cpp:1616`) |
| `ringbufferRAWLoraRX` | `MAX_LOG × 260`      | 5,200 | RX path                                                                      |

Total ~28.1 kB. Mechanically the safe form is a pointer-to-array
(`unsigned char (*ringBuffer)[UDP_TX_BUF_SIZE+5]`) allocated once in setup —
indexing `ring[i][j]` stays byte-identical, only the symbol's type changes.

**Risks, in order of bite (each needs an explicit verdict before implementation):**

1. **PSRAM trap (T-Deck class).** With `CONFIG_SPIRAM_USE_MALLOC` a plain
   `malloc` can land the ring in PSRAM, and `ringBufferUDPout` is reachable
   from `OnRxDone` — PSRAM access from ISR context crashes the ESP32. The
   allocation must be `heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`
   on ESP32, and the assessment must confirm which contexts really touch each
   ring on both platforms (the `OnRxDone` reachability comment at
   `udp_functions.cpp:1616` is the known one; audit for others).
2. **Init order.** Anything that touches a ring before the setup-time
   allocation dereferences NULL — static constructors, early BLE callbacks,
   the nRF52 timer task. Audit: no ring access before `init_ring_alloc()`
   runs; allocation must precede radio/BLE init.
3. **`sizeof` traps.** `sizeof(ringBuffer)` on the new pointer type is 4/8,
   not 5,200 — today that idiom lives in three native test fixtures
   (`test_txring`, `test_txring_flood:55,423`, `test_unconfigured:55`) and
   must be hunted repo-wide (`memset(ring, 0, sizeof(ring))` compiles and
   silently clears 8 bytes).
4. **Zero-init semantics.** `= {0}` today; read paths assume zeroed status
   bytes. The allocation must be `calloc`/`memset`, and the NATIVE_BUILD twin
   definitions in `txring_functions.cpp:27` must move to the same alloc path —
   otherwise the tests keep testing the static layout while hardware runs the
   heap one.
5. **OOM at boot** cannot realistically fire (~28 kB from a >100 kB heap at
   boot) but must still be handled loudly: `[MEM];alloc;fail` + reboot, never
   a silent NULL.
6. **Upstream friction.** The type change in `loop_functions_extern.h`
   touches every including file's recompile; the PR must stay mechanical
   (pointer-to-array, one alloc function) to be reviewable.

**Regression bar (must all pass before this merges — operator requirement):**

- Native: the alloc path itself compiled and used in the native envs (not the
  static twin), so `sizeof`/type traps surface in CI; the heap rings then also
  sit behind ASan redzones in the `-fsanitize` envs, which is a strict
  upgrade over static arrays for overflow detection. Full native gate green
  (10 envs), plus a grep gate: zero remaining `sizeof(<ring symbol>)`.
- Hardware, all four bench nodes: boot, cross-board LoRa TX/RX, BLE phone
  connect + message, TM-31 `gwflood` (exercises `ringBufferUDPout` under
  load), BP-01 burst incl. QRT at 16/20, one OTA (TM-40), and a ≥10-minute
  soak with `[MEM];boot;heap_free` before/after showing the heap paid for it
  and is stable (no creep). `resource_watch.py dram` documents the headroom
  gain (~28 kB → classic ESP32 at roughly 40 kB headroom).
- The `dram` CI gate stays at 4 kB — MEM-02 buys room, it does not retire the
  guard.

### 3.8n Heltec V3 battery reading (intake 2026-08-30 late)

| ID     | Board(s)                         | Type | Sev.   | Location                                                                                         | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ------ | -------------------------------- | ---- | ------ | ------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| BAT-01 | Heltec V3 (ADC boards generally) | BUG  | Medium | `src/batt_functions.cpp` (ADC read + `ADC_CTRL_PIN` toggle :64-139), display %, phone/BLE status | **Unstable battery % on the display with NO battery attached** (operator observation on DK5EN-93; measured: the TM-38 run logged 844 `readBatteryVoltage` samples in 16 min ranging **3,716-4,886 mV** -- a 1.17 V second-to-second swing). With no cell on the connector the VBAT divider node floats and the ADC reads noise; the "no battery is connected" detection exists only on the PMU boards (`esp32_main.cpp:3398`, AXP path -- T-Beam), the plain-ADC boards have none, so the jitter is converted straight into a jumping percentage. Fix direction: detect the no-battery case on ADC boards (implausible sample-to-sample delta and/or out-of-band voltage while USB-powered) and show "USB"/"--" instead of a %; secondarily check the settle time after `ADC_CTRL_PIN` enables the divider before sampling. Smoothing alone would fake a stable % where no battery exists -- detection first. Side note: the read runs ~1x/s and, with `--debug on`, prints three log lines per read -- worth rate-limiting while in there. | **FIXED 2026-08-31** (`b624bd33`, Wave 2): pure hysteresis detector (250 mV raw-sample delta + plausibility band scaled to `fBattMax`, 6 samples to absent / 10 back), feeds the existing `global_batt==0` -> "USB" convention; `battHardwarePresent()` includes the verdict (stops the `/B=` tag). **Mirrored into `batt_function_old.cpp`** — Heltec V3/V4 compile that file, not `batt_functions.cpp` (duplicate marked keep-in-sync). Web row shows "USB (no battery)", T-Deck header fixed. Settle delay added on real OFF->ON only. 8 native cases (`native_batt_detect`, ASan/UBSan). **Bench proof 2026-08-31:** DK5EN-93 raw samples swing 3745-4744 mV (floating divider, no cell) while the reported value is forced to 0.00 V; T-Deck-14 web row now shows `USB (no battery)` instead of `4.847V (100%)`. Known gap: RAK/nRF52 branch shares the architectural gap, unverified, not touched |

---

### 3.8o Web-UI info page and page JavaScript (intake 2026-08-30 late, operator screenshots)

Operator intake from three screenshots of `dk5en-14.local` (T-Deck Plus, 4.35p): the info
page's NTP row is empty, the connected AP's BSSID is missing next to the SSID, the MHeard
page shows `NCNT: 0` for DK5EN-90 (RAK4631, no WiFi, LoRa active — it demonstrably hears
the other three bench nodes), and a general review of the page JavaScript was requested.

| ID     | Board(s)          | Type | Sev.   | Location                                                                                                                                       | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Status                                                                                                                                                                                                                                                                                                                                                                                                                                |
| ------ | ----------------- | ---- | ------ | ---------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| WEB-01 | ESP32 (info page) | BUG  | Low    | `src/web_functions/web_functions.cpp:1840`                                                                                                     | **"NTP address" row is empty** unless the user has set an own NTP override: the row prints `node_ownntp` (the `--setownntp` override), not the NTP server the node actually uses. The effective server is chosen in `udp_functions.cpp:1495-1536` (`node_ntp` if set, else `pool.ntp.org` on internet, else a Hamnet literal). Fix: print the effective server (override, else the chosen default), optionally tagged "(default)".                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | **FIXED 2026-08-31** (`81cfc064`, Wave 1): `getEffectiveNtpServer()` mirrors the udp_functions selection; live on DK5EN-14: `pool.ntp.org (default)`                                                                                                                                                                                                                                                                                  |
| WEB-02 | ESP32 (info page) | ENH  | Low    | `src/web_functions/web_functions.cpp:1810-1821`                                                                                                | **BSSID missing next to the WiFi SSID** — with several APs broadcasting the same SSID (mesh sets like the bench ORBI) you cannot see which AP the node associated with. In STA mode print `WiFi.BSSIDstr()` (and it is useful next to RSSI when chasing roaming issues, cf. TM-34/TM-38). N/A in AP mode and on the RAK.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | **DONE 2026-08-31** (`81cfc064`, Wave 1): `WiFi BSSID` row via `WiFi.BSSIDstr()`, STA mode only; live on DK5EN-14: `5A:AF:97:2E:2B:8B`                                                                                                                                                                                                                                                                                                |
| NC-01  | nRF52 first, all  | BUG  | Medium | `src/mheard_functions.cpp:600-616` (`getMheardCount`), `src/loop_functions.cpp:528` (`getUnixClock`), beacon builder `loop_functions.cpp:4007` | **NCNT 0 announced by a node without a valid clock.** NCNT in MHeard is not computed by the viewer — it is the `/N<n>` tag the _remote_ node appends to its own position beacon, counting its mheard entries **of the last hour** (`mheardEpoch[i]+3600 > getUnixClock()`). DK5EN-90 has no WiFi/NTP and no GPS, so its date fields are unset; `getUnixClock()` runs `mktime()` on `tm_year = -1900`, which fails (returns `(time_t)-1` → huge unsigned value) or yields garbage, `mheardEpoch[i]+3600` wraps, every entry looks stale, `getMheardCount()` returns 0 and no `/N` tag is sent — peers keep the default 0. Suspected root cause, verify on DK5EN-90 (`--mheard` count vs. displayed NCNT, and its date). Fix direction: with no valid wall clock, age mheard entries by a monotonic source (`millis()`-derived) instead of the broken epoch — same clock-vs-count lesson as the dedup ring. Same wrap hazard sits in the 12 h prunes at `mheard_functions.cpp:260/642/720`.                                                                                                                                                                                                                                                                                                     | **FIXED 2026-08-31** (`81cfc064`, Wave 1): monotonic `mheardMillis[]` aging (rollover-safe u32), 1 h count + all three 12 h prunes converted; T-Deck persistence load seeds ages; native regression `test/test_mheard_aging/` with `getUnixClock()` stubbed broken. **Bench proof 2026-08-31:** DK5EN-90 flashed, one heard neighbour, `--sendpos` -> T-Deck-14 MHeard shows `NCNT: 1` for DK5EN-90 (was 0). Remainder split to NC-02 |
| WEB-03 | ESP32 (web UI)    | SEC  | High   | `src/web_functions/web_functions.cpp:1624/1637` (messages), `:853` (login), `:857` (send), `:863` (setvalue)                                   | **Web-UI JavaScript review (requested).** Findings, worst first: **(a) Stored XSS** — received message payloads are printed into the messages page unescaped (`msgtxt.c_str()` straight into HTML, injected via `innerHTML` in `updateMessages()`); any LoRa/mesh sender can run script in the operator's browser (same session holds the login and `/config.json` with cleartext secrets). Needs server-side HTML escaping of payload, callsigns and paths (a prior audit flagged the same class at the JSON endpoint, `code-audit-20260508.md` HIGH). **(b)** `sendMessage()` uses `encodeURI`, not `encodeURIComponent` — `&`, `#`, `+` in a message truncate or corrupt it; `tocall` is not encoded at all. Same gap in `setvalue()` for text params. **(c)** `login()` puts the password in a GET query string (browser history, logs). **(d)** No XHR error/timeout handling and no in-flight guard: on a slow node the 10 s `autorefresh` piles up overlapping requests, and two rapid nav clicks can race so the slower response wins the wrong page. **(e)** Cosmetics: full `innerHTML` replace per refresh loses scroll position; `alert()`/`confirm()` block the page (and any browser automation). (a) and (b) are worth fixing; (c)-(e) are judgement calls given flash budget. | **(a)+(b) FIXED 2026-08-31** (`81cfc064`, Wave 1): `htmlEscape()` on payload/paths in the messages page, `encodeURIComponent` in sendMessage (incl. tocall) and setvalue (call sites verified un-encoded). (c)-(e) remain open judgement calls                                                                                                                                                                                        |

**Wave-1 follow-ups (filed 2026-08-31, found during the fix wave):**

| ID      | Board(s)        | Type | Sev.   | Location                                                                                                                        | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | Status                                                                                                                                                                                                                                                                       |
| ------- | --------------- | ---- | ------ | ------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| NC-02   | all             | BUG  | Low    | `src/via_functions.cpp:125`, `src/web_functions/web_functions.cpp:1222`, `src/mheard_functions.cpp:544/790` (`mheardPathEpoch`) | Remaining wall-clock aging sites NOT converted by NC-01: the via list and the web mheard page still compare `mheardEpoch[i]+window > getUnixClock()`, and `mheardPathEpoch[]` has the identical hazard. Display-only impact (wrongly hidden/shown entries on clockless nodes), the beacon `/N` count is fixed. Convert to the `mheardMillis[]` pattern (needs a small exported helper or extern).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | **FIXED 2026-08-31** (`a1191eaa`+`361ac9fe`): mheardFreshMs()/mheardPathFreshMs() exportiert, via-Liste, beide Web-Tabellen (auch die nicht gelistete :1277) und die mheardPathEpoch-Prunes monoton; via_functions-Stelle liegt in totem /_..._/-Block, trotzdem konvertiert |
| MH-02   | all             | BUG  | Low    | `src/mheard_functions.cpp:~300` (`updateMheard` eviction search)                                                                | Dead code: `imin` is initialized to `-1` and never assigned in the loop that computes `ulmin`, so the "evict the oldest entry" branch never runs — eviction always falls through to the sequential `mheardWrite` ring. Pre-existing, unrelated to NC-01; fix = actually assign `imin` where `ulmin` updates.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | **FIXED 2026-08-31** (`a1191eaa`): imin wird gesetzt, Eviction waehlt den monoton aeltesten Eintrag; Regressionstest per Revert-Gegenprobe                                                                                                                                   |
| ETH-01  | nRF52 (RAK4631) | BUG  | Medium | `src/nrf52/nrf52_main.cpp:1963ff` (`if(bGATEWAY)` block)                                                                        | `ethLinkPoll()`/`ethLinkHeartbeat()` and the DHCP lease refresh are nested inside the `bGATEWAY` block — a gateway-off/webserver-on node (possible per `nrf52_main.cpp:1086`) never renews its DHCP lease and has no link-state instrumentation. Sibling of TM-45, larger gating question; found during the TM-45 fix.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | **FIXED 2026-08-31** (`a1191eaa`): Link-Poll/Heartbeat/DHCP-Refresh vor den bGATEWAY-Block gehoben; Gateway-Pfad unveraendert                                                                                                                                                |
| CONF-01 | nRF52 (RAK4631) | BUG  | Medium | `src/nrf52/nrf_eth.cpp:668-780` (CONF branch), reference `src/udp_functions.cpp:533-586`                                        | The nRF52 CONF handler parses the server-pushed callsign/shortname/coords into local `String`s only — it never applies them to `meshcom_settings`, never saves, never reboots. Server-side provisioning of an nRF52 gateway is therefore a silent no-op. Found during TM-39 ESP32-CONF recon. **Enriched 2026-08-31 (intake point 11, §3.8p):** parse is correct and complete (callsign `:730`, shortname `:743`, lat/lon/alt `:753`/`:761`/`:769`); every later use of those locals is absent — apply, `save_settings()` and `rebootAuto` are all missing, block ends `:783`. The ESP32 path to mirror also has two safeguards the nRF52 side lacks entirely: the source-IP guard (`udp_functions.cpp:533`, apply only from the resolved server) and `checkRegexCall()` on the callsign (`:566`). No `FLASH_STRUCT_VERSION` bump and no fleet wipe — every target field already exists. No test covers the apply step on either platform. Endianness/compatibility documentation split out as `DOC-03`. | **FIXED 2026-08-31** (`a1191eaa`): geteilter parseConfFrame(), Guard remote_ip==udp_dest_addr, checkRegexCall, save_settings(), 15 s Auto-Reboot; Koordinaten weiterhin nur geloggt                                                                                          |

---

### 3.8p Intake 2026-08-31 (operator list of 14 points)

Fourteen points handed over as a block, all of them backlog items — nothing in this section is
implemented yet. Each point was scouted read-only before it was written down (14 parallel
discovery agents, `/orchestrate-waves`), so every row below carries the file:line an implementer
starts from. **Three of the fourteen premises did not survive the scouting** and are corrected in
place: point 7 (foreign DMs are already blocked on BLE/web — the leak is EXTUDP), point 11 (TM-39
is a closed test item; the defect is the open `CONF-01`), point 14 (there is no low-voltage TX
inhibit anywhere in the tree, and the frame already carries an FCS the receiver checks). The
corrections were put to the operator and the answers are recorded in the rows.

Operator decisions taken during intake, binding for the rows below:

- **Filter policy (points 4/5/6):** drop the offending characters silently, deliver the rest; log
  the dropped bytes as hex in the debug log. Not a placeholder, not a whole-message reject.
- **Charset rule:** mirror MCProxy 1:1, including its known gap (see `CHR-01`).
- **Relay stays transparent:** filtering applies to what the node _outputs_ and to what it
  _originates_ — a relayed foreign frame is passed on byte-identical.
- **`NoPMOther` (point 7):** gate the EXTUDP path only; no Web-GUI checkbox, no T-Deck button
  ("über BLE läuft schon alles richtig").
- **Point 9 deliverable:** a self-contained HTML file under `docs/presentation/`, like
  `meshcom-protocol.html`.
- **Point 10:** the clean-ship vs. developer split is **not yet decided** — the help text and the
  markdown are written without that separation for now (see `DOC-02` for why the fault line is not
  where it looks).
- **Point 11:** `CONF-01` stays the implementation row and is enriched below; the endianness and
  compatibility documentation gets its own number (`DOC-03`).

| ID      | Point | Board(s)                 | Type | Sev.   | Item                                                                               | Status                                                                                                                                                                                                                                                                     |
| ------- | ----- | ------------------------ | ---- | ------ | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| TD-07   | 1     | T-Deck / T-Deck Plus     | ENH  | Medium | Map pan on four keyboard keys (`i`/`j`/`k`/`l`) plus a recenter key                | **DONE 2026-08-31** (`361ac9fe`): Pan i/j/k/l (1/4 Viewport), Recenter o, vier Auto-Recenter-Stellen gegated (inkl. esp32_main 30-s-Poll); ohne Tile-Cache 0,33-0,79 s/Schritt akzeptiert (Operator-Entscheid) -> TD-09. Bench-Skript im Agent-Report, Hardware-Test offen |
| PRES-01 | 2     | —                        | DOC  | Low    | Meshtastic and MeshCore on the deck timeline, with the ISM/amateur-radio footnote  | **DONE 2026-08-31** (Docs-Commit): Meilensteine + BNetzA-Fussnote + AFuV Par. 16 Abs. 8 woertlich (doppelt gefetcht) in meshcom-protocol.html; Deck enthielt vorher keine dieser Angaben (add statt fix)                                                                   |
| PRES-02 | 3     | —                        | DOC  | Low    | WSPR, WSJT-X and JS8Call on the deck timeline                                      | **DONE 2026-08-31** (Docs-Commit): fuenf Digimode-Meilensteine, Achse 8->15 Punkte, viewBox 960->1360                                                                                                                                                                      |
| CHR-01  | 4     | all                      | ENH  | Medium | Restrict the displayable character set of message text, RX (LoRa + UDP) and TX     | **FIXED 2026-08-31** (`361ac9fe`): UTF-8-Allowlist (Operator-Entscheid) in src/charset_filter.*, Chokepoints decodeAPRS/encodePayloadAPRS; 15 native Faelle, APRS-Korpus unveraendert gruen                                                                                |
| JSN-01  | 5     | all                      | BUG  | High   | JSON the node builds must stay valid — escaping and buffer bounds                  | **FIXED 2026-08-31** (`361ac9fe`): drei Web-Endpunkte streamen serializeJson; EXTUDP ohne strEsc-Doppel-Escape, sizeof(buffer)-Bound; 13 BLE-Builder ueber sendBleJsonRegister() + bleJsonFrameFailSoft() (Feld-Drop statt Mid-String-Cut); +3 native Faelle               |
| CHR-02  | 6     | all                      | ENH  | Medium | Same filter for the APRS free-text fields, without breaking the wire delimiters    | **FIXED 2026-08-31** (`361ac9fe`): Separator-Strip-Modus ({ } : ; , /) an allen drei atxt-Frame-Build-Stellen, UTF-8-sichere Truncation bei 25/16 Byte; --setatxt-Speicherstelle bewusst ungefiltert (Frame-Build deckt alle Writer)                                       |
| PM-01   | 7     | all with `--extudp on`   | ENH  | Medium | `NoPMOther`: stop handing foreign DMs to the EXTUDP client                         | **DONE 2026-08-31** (`d4f875e9`): Bit 0x8000 in node_sset3, --nopmother on                                                                                                                                                                                                 | off, Filter am sendExtern()-Chokepoint (lora/udp/node). **Polaritaet: 0 = heutiges Verhalten (Flotte unveraendert), 1 = unterdruecken — Opt-in statt des im Intake angedachten neuen Defaults** (stiller Flotten-Flip waere die groessere Ueberraschung) |
| APRS-01 | 8     | all (paper)              | DOC  | Medium | Research paper: forwarding received APRS data to YAAC / aprs.fi app / APRSdroid    | open                                                                                                                                                                                                                                                                       |
| DOC-01  | 9     | all (page)               | DOC  | Medium | HTML timeline of the main loop with the confirmed stalls, upstream vs. this branch | **DONE 2026-08-31** (Docs-Commit): docs/presentation/main-loop-timing.html; RX-Verlustmodelle getrennt, kein erfundener Stall-Durchsatz                                                                                                                                    |
| DOC-02  | 10    | all                      | BUG  | Medium | `--help` is ~60 commands short, has one unusable line and lies on five others      | **DONE 2026-08-31** (`d4f875e9`, nur Text per Operator-Entscheid): U+2212-Fix, 5 T-Deck-Kommandos gegated, --SS raus, Hilfe 95 -> 145 Kommandos, ehrliche INSTRUMENT_ENABLED-Notiz; +2480 B Flash (Heltec 42,2 %)                                                          |
| CONF-01 | 11    | nRF52 (RAK4631)          | BUG  | Medium | nRF52 CONF handler parses but never applies — **row enriched below**               | **FIXED 2026-08-31** (`a1191eaa`): geteilter parseConfFrame(), Guard remote_ip==udp_dest_addr, checkRegexCall, save_settings(), 15 s Auto-Reboot; Koordinaten weiterhin nur geloggt                                                                                        |
| DOC-03  | 11    | nRF52 + ESP32            | DOC  | Medium | Endianness and settings-compatibility documentation for the CONF/country path      | **DONE 2026-08-31** (Docs-Commit): docs/wire-compat.md; Korrektur: 2x htons (sin_port) statt null Treffer                                                                                                                                                                  |
| DOC-04  | 12    | ESP32 + nRF52            | DOC  | Medium | Reference markdown for all 107/103 `config.json` registers                         | **DONE 2026-08-31** (Docs-Commit): docs/settings-registers.md; 107/103 bestaetigt, node_gpsbaud tot, 4 Namensdreher, 5 Secrets (node_opwd/node_passwd bisher undokumentiert), 6 Boot-Clamp-Divergenzen                                                                     |
| NTP-01  | 13    | ESP32 + nRF52            | ENH  | Medium | NTP cadence answered, report, `--ntpsync` command, bench regression                | **DONE 2026-08-31** (`d4f875e9`): docs/ntp-timing.md, --ntpsync (mit [NTPSYNC];busy via NtpAsync::isPending()), Bench-Skript tools/bench/experiments/ntpsync.py (Live-Lauf noch offen)                                                                                     |
| E22-01  | 14    | E22 (classic ESP32 + S3) | BUG  | High   | Frame integrity under supply spikes / RF ingress — concept for operator review     | open                                                                                                                                                                                                                                                                       |

Two defects and one divergence found by the scouts on the way, filed as follow-ups:

| ID     | Board(s)             | Type | Sev.   | Location                                                                       | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | Status                                                                                                                                                                                                                                          |
| ------ | -------------------- | ---- | ------ | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| WEB-04 | ESP32 (web UI)       | BUG  | Low    | `src/web_functions/web_setup.cpp:407-412`                                      | The `sendpos` web parameter handler issues `"--nomsgall %s"` — a copy-paste of the block directly above it. Setting `sendpos` from the web UI therefore toggles the broadcast-display flag instead. Found while mapping the switch plumbing for `PM-01`; do not use that block as a template.                                                                                                                                                                                                                                                                                                           | **FIXED 2026-08-31** (`361ac9fe`): sendpos-Setparam-Handler feuert sendpos/sendtrack statt --nomsgall                                                                                                                                           |
| TD-08  | T-Deck / T-Deck Plus | BUG  | Low    | `src/t-deck/tdeck_main.cpp:774-817`                                            | `iKeyBoardType == 4` (the on-screen "sym" input mode, toggled at `event_functions.cpp:799-821`) remaps every `a`-`z` byte to a symbol **before** the map-tab dispatch at `:851-861` — so the existing `g`/`h` zoom shortcuts are already dead in that mode. Pre-existing, inherited by `TD-07`'s new keys.                                                                                                                                                                                                                                                                                              | **FIXED 2026-08-31** (`361ac9fe`): Map-Tasten dispatchen vor dem Sym-Remap, g/h-Zoom im Sym-Modus wieder aktiv                                                                                                                                  |
| TD-09  | T-Deck / T-Deck Plus | ENH  | Medium | T-Deck Karte (`tdeck_sdmap.cpp`)                                               | Tile-Cache fuer die Karte (PSRAM): ohne ihn kostet jeder TD-07-Pan-Schritt 0,33-0,79 s (PNG-Decode ~170 ms/Kachel dominiert). Von TD-07 abgespalten per Operator-Entscheid 2026-08-31.                                                                                                                                                                                                                                                                                                                                                                                                                  | open                                                                                                                                                                                                                                            |
| TD-10  | T-Deck / T-Deck Plus | ENH  | Low    | `src/t-deck/tdeck_main.cpp` (`keypad_get_key`/`keypad_read`, :749/:782)        | Auto-Repeat fuer Backspace (Taste halten = mehrfach loeschen). Analyse abgeschlossen, Konzept liegt: LVGL-Keypad-Repeat (`lv_indev.c:488-514`) existiert schon, es fehlt nur die Halte-Erkennung — die liefert der Raw-Mode der Stock-Keyboard-Firmware (I2C-Cmd `0x03`/`0x04` an `0x55`, 5-Byte-Matrixframe, Backspace = Byte 4 Bit 3); Degradation auf heutiges Verhalten, falls das Keyboard den Cmd nicht kennt. ~40-60 Zeilen, nur `tdeck_main.cpp`; Bit-Polaritaet am DK5EN-14 zu verifizieren. Volle Analyse: [`tdeck-backspace-autorepeat-20260831.md`](tdeck-backspace-autorepeat-20260831.md) | **IMPLEMENTIERT 2026-09-02** auf `feat-tdeck-keyrepeat-20260902` (Worktree `mc-keyrepeat`), 39 native Testfaelle, Review-Verdikt `docs/review-verdict-tdeck-keyrepeat-20260902.md`; Bench-Nachweis am Operator-Geraet (Umsetzungsplan §7) offen |
| TD-11  | T-Deck / T-Deck Plus | ENH  | Low    | `src/t-deck/lv_obj_functions.cpp:160` (`MsgBubble`), `:4383` (`tdeck_add_MSG`) | No send/delivery indicator in the T-Deck message view: the bubble carries header, timestamp and body, but no `msg_id` and no status, so the heard/ACK state that `own_msg_id[i][4]` already tracks cannot be shown. Web UI shows it (`web_functions.cpp:1645-1658`, `✓` heard / `☑` ACK), the phone app gets it over BLE (`MSG_TYPE_ACK` 0x41, byte 5). Operator report 2026-09-03, analysis in §3.8w.                                                                                                                                                                                                  | open                                                                                                                                                                                                                                            |
| BAT-02 | nRF52 (RAK4631)      | BUG  | Medium | `src/batt_function_old.cpp` (NRF52_SERIES-Zweig)                               | Die RAK/nRF52-Batterieerkennung teilt die BAT-01-Luecke: keinerlei No-Battery-Erkennung im nRF52-`read_batt()`-Zweig (Intake-Nachtrag des Operators 2026-08-31).                                                                                                                                                                                                                                                                                                                                                                                                                                        | **FIXED 2026-08-31** (`a1191eaa`): Detektor-Guard auf den RAK4631-Zweig geweitet (T114/T-Echo ausgenommen); 30-s-Lesetakt -> Erkennung nach ~3 min; Bench-Proof auf DK5EN-90 offen                                                              |
| CTY-01 | nRF52 (RAK4631)      | BUG  | Low    | `src/nrf52/nrf_eth.cpp:1163` vs. `:1279-1292`                                  | The two nRF52 server-selection paths disagree: `startFIXUDP()` (static IP) special-cases `IT` on the internet path (`145.239.75.155`, `:1287`), while `startUDP()` (DHCP — the path the bench RAK actually takes) has no country case at all and always uses `89.185.97.38`. `docs/bench-country-servers.md` only tabulates the DHCP path, so this divergence is undocumented.                                                                                                                                                                                                                          | **FIXED 2026-08-31** (`a1191eaa`): startUDP() Internet-Zweig mit IT/else-Split wie startFIXUDP(); bench-country-servers.md ergaenzt                                                                                                             |

---

#### TD-07 — map pan on four keys (point 1)

**Decision:** `i` = up, `j` = left, `k` = down, `l` = right; a fifth key recenters on the own
position, and the pan otherwise persists (operator choice; `o` is free and adjacent, the trackball
click is the alternative).

**Where the keys are dispatched.** `keypad_read()`, `src/t-deck/tdeck_main.cpp:722-908`, is the only
global key hook — no other file registers an `LV_EVENT_KEY` handler. Raw bytes come from the I2C
keyboard at `0x55` via `keypad_get_key()` (`:689-716`); the special-key if-chain at `:825-889`
consumes a key by setting `bSPEC` instead of passing it to the focused widget. The map-tab gate
already exists and is reused verbatim: `lv_tabview_get_tab_act(tv) == 3` (`:852`, `:857`; tab 3 =
MAP per `tdeck_debug.h:55`). Tabs 1 and 7 are excluded from all special handling (`:827`).

Bound today: `g`/`G` zoom in (`:852`), `h`/`H` zoom out (`:857`) — map tab only; `"` backlight lock
(`:830`), `'` keyboard lock (`:839`), `+`/`-` zoom (`:863`/`:869`), `!` brightness (`:875`), `.`
mute (`:881`) — everywhere except tabs 1 and 7. Every other unshifted letter is free.

**What actually has to be built.** The map has no pan state at all. `sdmap_refresh(img, lat, lon)`
(`src/t-deck/tdeck_sdmap.cpp:224-359`) recomputes `sdmap_originX`/`sdmap_originY` (`:165-166`) from
the passed coordinates on every call (`:276-279`), and all four existing call sites pass the own
position (live GPS → cached `sdmap_lastKnownLat/Lon` → configured `node_lat/lon`). So:

1. New persistent view-centre state (a "panned" flag plus either a virtual lat/lon or a pixel
   offset). Only the forward projection exists today (`sdmap_lon2xf`/`sdmap_lat2yf`, `:33-42`); a
   lat/lon design needs new inverse-Mercator helpers, a pixel-offset design needs a new entry point
   that bypasses the hard-coded recentre at `:276-279`.
2. Gate the four auto-recentre sites so they stop fighting the pan: `event_functions.cpp:885-899`
   (tab switch to MAP), `lv_obj_functions.cpp:1962-1996` (`set_map`), `lv_obj_functions.cpp:3742-3754`
   (`tdeck_add_pos_point`, fires on an incoming own-position beacon), `esp32/esp32_main.cpp:3145-3170`
   (30 s tile-boundary poll while the MAP tab is active).
3. One shared `tdeck_map_pan(dx, dy)`, mirroring the `tdeck_map_zoom(dir)` consolidation
   (`lv_obj_functions.cpp:1912-1929`, which replaced four copies per
   `docs/tdeck-findings-20260828.md`) — the recentre logic is still copy-pasted in the four places
   above and should be consolidated in the same move.
4. Four new clauses next to the `g`/`h` block at `tdeck_main.cpp:851-861`, plus the recenter key.

**Cost, and the open call on it.** There is no decoded-tile cache: `sdmap_refresh()` re-reads and
re-decodes every intersecting tile from SD on every call (`sdmap_load_tile_rgba()`, `:175-219`, no
memoization). Measured in `docs/tdeck-findings-20260828.md` §5: **0.33-0.79 s per recompose** at the
current 20 MHz SD clock (`tdeck_main.cpp:296`), PNG decode dominating at ~170 ms/tile. Discrete
step-per-keypress is usable; held-key/continuous panning would serialise those costs into
multi-second stalls. **Open:** ship v1 at that latency, or make a small decoded-tile LRU in PSRAM
(same `ps_malloc` already used for `sdmap_buf`) a prerequisite. The doc already names the cache as
the missing piece ("a decoded-tile cache would make zoom-back instant").

Off-map edges need no new handling — missing tiles are painted grey with a `map_no_data_label`
(`tdeck_sdmap.cpp:248-255`, `:350-354`). Pan multiplies the SD-read-then-flush sequence, so the
lost-flush mitigation (`s_flushfix_on`, default on, `tdeck_debug.cpp:134-136`) must be regression-
tested under rapid repeated pan keypresses. `TD-08` (sym mode eats `a`-`z`) applies to the new keys
too.

#### PRES-01 — Meshtastic and MeshCore on the timeline (point 2)

Deck: `docs/presentation/meshcom-protocol.html`, 4262 lines, self-contained (inline CSS/JS, one
Google-Fonts link), German, no build step, no generator. **Neither Meshtastic nor MeshCore is
mentioned anywhere in it today** — this is a pure addition.

The timeline is two structures that must be edited in sync: hand-placed SVG (`:1093-1229`, one
`<g class="cmp-tl-ms" data-ms="N">` per milestone, viewBox `0 0 960 290`, axis at y=150, entries
alternating above/below) and the JS `MS[]` array (`:2681-2691`) that drives the detail card, the
runner dot and the era colouring. Order is strictly left-to-right by `x`, and `data-ms="N"` must
match the array index. Eight entries today: 1976 X.25 (x=75), 1976 Bell-202 (152), 1982/84 AX.25
(248), 1992 APRS (393), 2012 Semtech/Cycleo (628), 2013 SX1272/76 (700), 2015 LoRa Alliance (758),
heute MeshCom (882). There is no auto-layout — the axis spans x=62-895 and is already dense from
628 on, so new entries mean recomputing existing `x` values by hand. A new era needs new CSS classes
(`.cmp-tl-dot-*`, `.cmp-tl-rring-*`, `.cmp-tl-rdot-*`, `.cmp-tl-era-lbl-*`, around `:207-322`).

**Dates, verified against primary sources (GitHub API / official texts), not blogs:**

| Project    | Fact                                                                                        | Date       |
| ---------- | ------------------------------------------------------------------------------------------- | ---------- |
| Meshtastic | `meshtastic/firmware` repo created (first verifiable artifact), Kevin Hester "geeksville"   | 2020-02-01 |
| Meshtastic | earliest tag `0.0.3`                                                                        | 2020-02-20 |
| Meshtastic | licence GPL-3.0                                                                             | —          |
| MeshCore   | repo created (id 919192489, formerly `ripplebiz/MeshCore`), first commit Scott Powell 01-24 | 2025-01-19 |
| MeshCore   | first tagged releases (`companion-v1.0.0a/b/c`, `repeater`, `room-server`)                  | 2025-03-05 |
| MeshCore   | licence MIT                                                                                 | —          |

Two disagreements to handle honestly on the slide: secondary sources put the Meshtastic _idea_ in
2019 (no primary evidence found), and Wikipedia dates MeshCore to "late 2024" against a repo created
2025-01-19. Use the verifiable dates; mention the earlier claim only if the slide has room for the
caveat. Liam Cottle is credited by the community for the MeshCore companion clients — unverified
from commit data, so leave the attribution at Scott Powell or omit names.

**The footnote.** Both projects are ISM-band, not amateur radio; on ISM they may encrypt, but they
pay for it in power and duty cycle. Verified numbers (BNetzA Vfg 91/2025, implementing
2006/771/EC as amended by (EU) 2025/105; harmonised standard ETSI EN 300 220-2 V3.2.1):

- 868.0-868.6 MHz: **25 mW ERP**, LBT+AFA or **≤1 % duty cycle** (item 48).
- 869.4-869.65 MHz — the Meshtastic `EU_868` default, centre 869.525 MHz: **500 mW ERP**, LBT+AFA
  or **≤10 % duty cycle** (item 54).
- Amateur 70 cm in DL, AFuV Anlage 1 row 18: Klasse A **750 W PEP**, Klasse E 75 W PEP, Klasse N
  6.1 W ERP — and AFuV § 16 Abs. 8 verbatim: _"Der Amateurfunkverkehr darf nicht zur Verschleierung
  des Inhalts kodiert oder verschlüsselt werden"_ (exception only for satellite/remote-station
  control signals). Austria: AFV § 20 Abs. 5, _"Die Verwendung von Einrichtungen, die die
  Verständlichkeit der Nachricht einschränken, ist nicht gestattet."_
- **Do not claim a 33 cm band for DL/OE** — 902-928 MHz is an ITU Region 2 amateur allocation and
  does not exist in Region 1; the AFuV table jumps from 430-440 MHz straight to 1240-1250 MHz.
- Worth one line of nuance: both projects ship a _publicly known_ default key (Meshtastic's stock
  `AQ==` on LongFast; MeshCore's documented public-channel key), so out of the box they are
  encrypted but not confidential.

Footnote mechanism: reuse the existing `.note.warn` callout (CSS `:110-112`, live example at
`:2142` — which already carries the "Verschlüsselung ist auf Amateurfunk-Frequenzen ohnehin nicht
zulässig" line this footnote extends) or the terser `.cite` line (CSS `:116`, example `:1284`).

#### PRES-02 — WSPR, WSJT-X and JS8Call on the timeline (point 3)

Same two-structure edit as `PRES-01`. Verified dates:

| Mode / program | Fact                                                      | Date                           |
| -------------- | --------------------------------------------------------- | ------------------------------ |
| WSJT           | first release, Joe Taylor K1JT                            | 2001                           |
| WSPR           | first release, K1JT                                       | 2008                           |
| WSJT-X         | v0.1 r2695, first experimental build                      | 2012-10-25                     |
| WSJT-X         | v1.0                                                      | 2013-05-30                     |
| FT8            | announced in WSJT-X 1.8.0-rc1, Taylor K1JT + Franke K9AN  | 2017-06-29                     |
| JS8Call        | proposed as "FT8Call" on the WSJT-X list                  | 2017-07 (exact day unverified) |
| JS8Call        | experimental repo `jsherer/ft8call`, Jordan Sherer KN4CRD | 2018-03-05                     |
| JS8Call        | 1.0.0-RC1                                                 | 2019-02-22                     |
| JS8Call        | v1.0.0 general availability                               | 2019-04-01                     |

Trap to avoid: Wikipedia's "since 2005 … open source" for WSJT is the **GPL licensing** date, not a
release date. Layout consequence: five entries land between 1992 (x=393) and 2019, i.e. squarely in
the stretch that is already dense from x=628; either the whole axis is re-spaced or the HF digital
modes get their own lane/era band with its own colour classes.

#### CHR-01 — displayable character set for message text (point 4)

**Policy (operator):** drop the offending characters silently and deliver the rest; log what was
dropped as hex in the debug log. **Rule (operator):** mirror MCProxy 1:1.

MCProxy's rule lives in exactly one place, `MCProxy/src/mcapp/udp_handler.py:176-229`
(`is_allowed_char`) plus `:232-237` (`strip_invalid_utf8`, decode with `errors="ignore"` — invalid
UTF-8 is dropped, never repaired), and it runs on the **UDP receive path only** (port 1799, on the
whole decoded datagram before JSON parsing), never on send and never on BLE ingress. Allowed:
printable ASCII `0x20-0x7E`; a fixed literal set of umlauts/accented Latin letters plus `⁰`; the
emoji glue codepoints `U+200D`, `U+FE0E`, `U+FE0F`, `U+20E3` and the flag-tag range
`U+E0020-U+E007F`; and as the sole catch-all, any character whose Unicode general category starts
with `S` (Symbol) or `P` (Punctuation) or whose name contains `EMOJI`. Rejected: surrogates
`U+D800-U+DFFF`, noncharacters (`…FFFE`/`…FFFF`), the three Private Use Areas — and everything else,
which is where all C0/C1 controls land, CR `0x0A`/`0x0D` and BEL `0x07` included. Rejection is
per-character, silent, DEBUG-logged (`udp_handler.py:228`); the message is never dropped as a whole.
Byte-length caps (150/159/160) are a separate, independent check (`MCProxy/src/mcapp/schemas.py`),
mirrored client-side in `webapp/src/components/chat/ChatInput.vue:24-26`.

**Known gap, mirrored deliberately:** that rule does **not** admit Japanese. Hiragana/Katakana/Kanji
are Unicode category `Lo` (Letter), which no branch accepts — there is no CJK range and no
Letter-category branch anywhere in either repo. The operator chose to mirror the rule as-is so node
and proxy discard identically; widening both to category `L` is a separate decision, and would have
to be made on the MCProxy side first. Note for whoever picks that up: the webapp has no message-body
character filter at all (only `sanitizeDst` for the destination field and `stripRedundantPercentHex`
for a firmware percent-encoding quirk), so it inherits whatever MCProxy passes through.

**The implementation problem to solve first:** the MCProxy rule is expressed in terms of
`unicodedata.category()`, which does not exist on the MCU. The real design task is an
MCU-affordable approximation — codepoint ranges rather than a category table — that is pinned
against MCProxy's own vectors (`MCProxy/src/mcapp/udp_parsing_tests.py:228-289` already asserts
umlauts survive, PUA/invalid bytes/lone surrogates drop, and the ZWJ/keycap/VS/tag glue combos stay
intact). Flash budget matters here: classic ESP32 is the constrained platform (see `APRS-01`).

**Chokepoints (verified).** Two cover everything in scope:

1. `decodeAPRS()` — `src/aprs_functions.cpp:122`, filter immediately after
   `aprsmsg.msg_payload = cConcat1;` (`:380`). This one site covers **both** RX transports, because
   LoRa RX (`lora_functions.cpp:556`) and UDP-from-server (`udp_functions.cpp:278`, `:719`,
   `nrf52/nrf_eth.cpp:464`) call the same decoder. Note the asymmetry it fixes: the path fields just
   above are already byte-range-checked (`<0x20 || >0x7E`, `:204` and `:302`) — only the payload is
   unconstrained.
2. `encodePayloadAPRS()` — `src/aprs_functions.cpp:1063-1070`, the single `memcpy` at `:1068` that
   puts text on the wire. Covers every TX composer, since serial (`esp32_main.cpp:4256`,
   `nrf52_main.cpp:2905`), BLE (`phone_commands.cpp:563-594` → `esp32_main.cpp:2948`), web
   (`web_functions.cpp:2192`), T-Deck (`event_functions.cpp:717`) and T-Deck Pro
   (`ui_deckpro.cpp:1805`) all converge on `sendMessage()` (`loop_functions.cpp:3457`) →
   `encodeAPRS()`.

`sendMessage()` itself is the optional third point: it is the earliest, transport-aware place to
reject, and the back-pressure notice machinery next to it (`loop_functions.cpp:3378-3483`, `BP-01`)
already knows how to tell the right sender something was refused.

**The catch that must not be missed.** Several receive paths forward the _raw pre-decode wire bytes_
and never go through `encodeAPRS()`: `addBLEOutBuffer(RcvBuffer, size)` (`lora_functions.cpp:986`,
`:1067`, `:1186`), `addNodeData(RcvBuffer, …)` (`:1252`), `addTxRingEntry(RcvBuffer, …, "rx_relay")`
(`:1335`) and the UDP relay at `udp_functions.cpp:479`. Those carry unfiltered bytes even with both
chokepoints in place. Per the operator's relay decision, the split is: the paths that **deliver**
(BLE out, EXTUDP, display, web list) must be rebuilt from the filtered `aprsmsg`; the paths that
**relay** (`addTxRingEntry(… "rx_relay")`, `udp_functions.cpp:479`, and the gateway upload
`addNodeData()`) keep passing the original bytes untouched.

Do not conflate the filter with the three existing, unrelated transforms: `utf8ascii()`
(`loop_functions.cpp:5188-5294`) is a display-only 2-byte UTF-8→Latin-1 downgrader that silently
drops every 3- and 4-byte sequence; `htmlEscape()` (`web_functions.cpp:1071-1089`) is HTML output
escaping (`WEB-03a`); `decodeURLPercentCoding()` (`web_functions.cpp:766`) and `sendMessage()`'s own
`%C2/%E2/%EF/%F0` decoder (`loop_functions.cpp:3549-3600`) are input transport decoding.

Length is safe in this direction — dropping only shrinks, so the latent
`cConcat1[UDP_TX_BUF_SIZE=255]` vs. `MAX_APRS_FRAME_SIZE=340` margin
(`docs/architecture/08-defect-catalogue.md:1417`) is not made worse. The hex log line belongs in
`printBuffer_aprs()` (`loop_functions.cpp:3142-3147`), which already sees every RX and TX text frame
(called from `lora_functions.cpp:567/570/609`, `udp_functions.cpp:343`, `loop_functions.cpp:3703`);
`printAsciiBuffer()` (`:3078-3102`) is the existing precedent for a control-byte-aware dump. Tests:
a pure-C++ module in the shape of `src/settings_sanitize.cpp` (`test/test_settings_sanitize`,
`[env:native]`), which exists precisely because it has no Arduino dependency.

#### CHR-02 — the same filter for APRS free text (point 6)

Same rule, same policy, different fields — and a much tighter set of things it must not break.
`decodeAPRS()`/`encodePayloadAPRS()` cover the message payload; this row covers the **position
comment** `pos_atxt` (`decodeAPRSPOS()`, `src/aprs_functions.cpp:629-647`, 25-byte cap at `:632`),
the configured comment `node_atxt` (`char[40]`, copied verbatim by `--atxt`/`--aprscomment`,
`command_functions.cpp:3395-3413`) and the `@` weather/HEY payloads.

**What the filter must not touch**, all verified with the code that depends on it:

- `>` separates source path from destination path (`aprs_functions.cpp:196`, written `:1051`).
- `,` separates callsigns inside a path (`:211-221`, `:309-314`).
- The payload-type byte itself (`:` `0x3A`, `!` `0x21`, `@` `0x40`) terminates the destination path
  (`:294`).
- `{` + digits is the DM ACK-id trailer (appended `loop_functions.cpp:3687`, consumed
  `lora_functions.cpp:906`, `udp_functions.cpp:406-459`); `:ack`/`:rej` anywhere in a payload is
  read as an ACK signal (`lora_functions.cpp:905-908`).
- `,` and `;` inside a HEY payload separate `count,rssi,snr` per hop
  (`appendHeySignalReport()`, `aprs_functions.cpp:1152-1157`, consumed `mheard_functions.cpp:455`).
- `/B= /A= /P= /H= /T= /O= /F= /Q= /G= /N= /C= /V= /Y=` are the position extension markers, scanned
  byte-by-byte at `aprs_functions.cpp:656-1011`; `N`/`S` and `W`/`E` terminate the lat/lon numeric
  fields (`:560`, `:596`).
- Callsign and path fields are **already** ASCII-gated (`:204`, `:302`) plus `checkRegexCall()`
  (`regex_functions.cpp:9`) — do not duplicate that.

The one real hazard: those scanners count **bytes**, so the 25-byte comment cap at `:632` can cut a
multi-byte UTF-8 sequence in half and hand a broken tail downstream. The filter must truncate on
codepoint boundaries. And if receive behaviour changes at all, the pinned test
`test_steuerzeichen_in_breitenangabe` (`test/test_decodeaprspos/test_decodeaprspos.cpp:226-238`,
which asserts a control byte inside the latitude text is _tolerated_) has to be updated deliberately,
not accidentally.

#### JSN-01 — the JSON the node builds must stay valid (point 5)

Two producers already do this correctly and are the reference: `out_add_jsonstr()`
(`src/config_json.cpp:441-465` — escapes `"`, `\`, `\n`/`\r`/`\t` and every control byte as
`\u%04x`, and fails soft by discarding the whole export rather than truncating it, `:427-436`,
`:523-528`) and `escape_json()` (`src/t-deck/tdeck_helpers.cpp:149`, full RFC 8259). Four producers
do not:

1. **Web endpoints, no escaping at all.** `call_function()` `web_functions.cpp:2234`, `setparam()`
   `:2268`, `getparam()` `:2321` build `{"%s":"%s"}` by `printf` from percent-decoded query-string
   input and from config strings that mirror `node_atxt`, `node_name`, SSID/password and the static
   IP fields. A single `"` breaks the response — no length threshold needed. This is the still-open
   HIGH from `docs/code-audit-20260508.md:158` (line numbers have shifted since).
2. **EXTUDP double-escapes.** `strEsc()` (`extudp_functions.cpp:811-822`) escapes `"` and `\` and
   the string is then escaped again by ArduinoJson on serialize (`:628`) — the client receives
   literal `\"`. Content corruption, and it inflates the string feeding (3).
3. **EXTUDP buffer bound is the document, not the buffer.**
   `serializeJson(cJson, c_json, measureJson(cJson) + 1)` into `char c_json[500]` at
   `extudp_functions.cpp:564`, `:588`, `:609`, `:649`. This is exactly the antipattern that
   `test/test_ble_json_frame`'s control case (`frame_bounded_by_document()`) exists to demonstrate —
   here unfixed, on a path whose `msg` field is `aprsmsg.msg_payload`, i.e. reachable by any
   unauthenticated LoRa peer.
4. **Thirteen BLE register builders, same bound bug plus mid-string truncation.**
   `serializeJson(doc, print_buff, measureJson(doc))` into `char print_buff[350]`
   (`command_functions.cpp:95`) at `:5335, 5385, 5495, 5585, 5832, 5860, 5902, 5930, 6007, 6091,
6128, 6165, 6192`; the result is then clamped to `MAX_MSG_LEN_PHONE-2` = 298 and clamped **again**
   to 245 inside `addBLEComToOutBuffer()` (`loop_functions.cpp:625-629`). Both clamps cut after
   serialisation, i.e. mid-string, producing JSON the phone cannot parse — with a live field trace in
   `docs/issue-ble-i-register-mtu-20260828.md` (`json.decoder.JSONDecodeError … column 242`) and the
   silently-dropped variant in `docs/issue-mh-json-size-budget-20260828.md`.

Already fixed and to be copied: `src/ble_json_frame.h:20-25` bounds by `sizeof(buffer)` (`UP-01`,
`b586daee`, regression-tested).

Minimal fix set: one shared escaper instead of three inconsistent ones (plus zero in
`web_functions.cpp`); `sizeof(buffer)` as the bound everywhere; drop `strEsc()`; replace mid-string
truncation with the fail-soft "drop optional fields and re-measure" pattern already prototyped in
`mheard_functions.cpp:409-412` and proposed in `docs/issue-ble-i-register-mtu-20260828.md` §5.2/5.3;
and name the three unnamed limits (298/245/255) as one constant. Regression tests: extend the
`test_ble_json_frame` canary pattern to the 13 command sites and to `sendExtern()` with a
quote/backslash-heavy payload, plus a `setparam`/`getparam` case with a `"` in the value.

Relationship to `CHR-01`: the character filter removes control bytes upstream and shrinks the blast
radius, but it does **not** solve this — `"` and `\` are legal message characters and stay.

#### PM-01 — `NoPMOther`: stop handing foreign DMs to the EXTUDP client (point 7)

**Premise corrected.** Foreign DMs are _already_ not forwarded over BLE or to the web message list.
`lora_functions.cpp:1047` forwards only `*` (gated by `bNoMSGtoALL`) or `CheckOwnGroup(destination)`;
DMs addressed to the own call are handled in the separate branch at `:865`. `CheckGroup()`
(`aprs_functions.cpp:27-50`) returns 0 for anything containing a non-digit, so a personal callsign
can never match it. The UDP-from-server path has the same gate (`udp_functions.cpp:378`,
`nrf52/nrf_eth.cpp:536`: `*` gated `||` own call `||` `CheckGroup > 0`). Verified directly in the
tree, not taken from a report.

**The path that does leak is EXTUDP.** `lora_functions.cpp:814` calls
`queueExtern("lora", RcvBuffer, size, rssi, snr)` for **every** TEXT/POSITION/HEY frame, and it sits
_before_ the destination is even extracted (`:858-859`). `sendExtern()`'s message branch
(`extudp_functions.cpp:613-649`) suppresses only group `100001` (telemetry). So an `--extudp on`
client — MCProxy, the webapp — sees every DM that crosses the air. That is the behaviour the
operator observed.

**Scope (operator decision):** gate EXTUDP only. The gateway upload to the central server
(`addNodeData()`, `lora_functions.cpp:1252`) stays untouched — that is the gateway's job, not the
client's view. No Web-GUI checkbox and no T-Deck button ("über BLE läuft schon alles richtig").

**Build.** The destination is available at the gate: `aprsmsg` is already decoded at `:556`, so the
condition can read `aprsmsg.msg_destination_call` at `:814` (or the equivalent check can sit inside
`sendExtern()`'s message branch). Forward when the destination is the own call, `*`, or an own
group; otherwise skip. Persist in a spare bit of `node_sset3` — free bits are `0x0001` (the retired
`bMHONLY`, commented out at `esp32_main.cpp:816`/`nrf52_main.cpp:584`) and `0x8000`; use `0x8000`.
**Pick the polarity so that the existing fleet's `0` means "filter active"**, since the new default
is on and every deployed node reads `0`. Serial command `--nopmother on|off` in the shape of the
`--nomsgall` blocks (`command_functions.cpp:1925-1939` / `:2027-2041`), help line near `:760`,
`--info` near `:5615`, `nsetdoc["NOPMOTHER"]` near `:6083`; load next to `bNoMSGtoALL` at
`esp32_main.cpp:817` / `nrf52_main.cpp:585`.

**No `FLASH_STRUCT_VERSION` bump and no fleet wipe** — a new bit inside an existing `int` changes
neither `sizeof(s_meshcom_settings)` nor any offset, which is what matters for the nRF52 raw-struct
blob (`nrf52_flash.cpp:391`); ESP32 stores `node_sset3` as an NVS key/value
(`esp32_flash.cpp:187`/`:465`) and simply reads `0` for the new bit. `config_json.cpp:147` already
serialises the whole field, so backup/restore needs no change.

**Nothing else breaks:** foreign DMs are never ACKed today (all ACK logic is inside the own-call
branch at `:865`); dedup and relay run before the destination dispatch and key on `msg_id`;
`updateMheard()` (`:739`) is unconditional, so the heard-station list stays complete. Do **not** copy
`web_setup.cpp:407-412` as a template — see `WEB-04`.

#### APRS-01 — research paper: node → YAAC / aprs.fi app / APRSdroid (point 8)

Scope set by the operator: all three clients. Ground truth the paper starts from, all verified:

**Nothing exists yet.** A whole-tree search for `>APRS`, `TCPIP`, `qAR`, `TNC2`, KISS and AGWPE finds
exactly one hit outside third-party libraries — a comment in `src/softser_functions.cpp:325-329`
showing the format of a TNC2 line the node _parses_ from an external sensor. There is no
serialiser, no KISS framing, no AGWPE, and no APRS-IS text output anywhere. What the wire format
actually is: MeshCom's own binary frame (`docs/architecture/11-wire-format.md` §1);
`encodeStartAPRS()` (`aprs_functions.cpp:1024-1058`) builds `src>dst` plus a type byte _inside_ a
6-byte binary header. `PositionToAPRS()` (`loop_functions.cpp:3807`) builds only the payload/comment
portion. aprs.fi compatibility is claimed in the deck only via the central-server gateway path.

**What the node holds to forward:** the mheard table — parallel arrays, not a struct array
(`mheard_functions.cpp:34-68`), `MAX_MHEARD` 30 on classic ESP32 / 80 on ESP32-S3 and nRF52840 / 50
XML / 10 T-Beam-dev (`configuration_global.h:191-229`), with last-known lat/lon/alt per heard station
updated at `lora_functions.cpp:661-694`, pruned at 12 h by `millis()`, persisted to SD only on
T-Deck. Plus the node's own position/sensor state.

**Transports available:** BLE Nordic UART (`6E400001-…`, `esp32_main.cpp:1661-1696`; nRF52 pins ATT
MTU to 250 at `nrf52_ble.cpp:91`), frame types `0x44` JSON — hard-clamped to 245 bytes including the
type byte (`BLE_JSON_PAYLOAD_MAX`, `configuration_global.h:348`; clamp
`loop_functions.cpp:621-627`) — `0x40` + raw frame + 4-byte big-endian timestamp, and a 13-byte ACK
frame; and ext-UDP JSON on the fixed port 1799 (`EXTERN_PORT`, `configuration_global.h:161`), one
peer via `--extudpip`, no authentication. The protocol is documented only in this repo's own
reverse-engineered `docs/architecture/11-wire-format.md`, which says of itself that it is not
upstream-normative.

**Budget, and a prerequisite.** The newest RAM snapshot in the tree,
`docs/ram-comparison-20260517.md`, predates `MEM-01` by 3.5 months — **a fresh snapshot is a
prerequisite for any credible cost estimate.** What is known: classic ESP32 is the wall — E22-DevKitC
`dram0_0` at 99.09 % and ttgo_tbeam `iram0_0` at 99.98 % (28 bytes free) in that snapshot; `MEM-01`
(`861f2967`) then freed roughly 10 kB on both (headroom E22 1712 → 11896 B, T-Beam 528 → 10712 B) by
cutting the classic-ESP32 rings, and added a 4 kB CI DRAM gate (`tools/resource_watch.py`).
`MEM-02` (five static rings, ~28.1 kB, `loop_functions.cpp:403-435`) is parked. Candidates for "we
cannot carry this": E22-DevKitC, ttgo_tbeam and its SX1262/SX1268 siblings, ttgo-lora32-v21,
heltec V2 (all classic ESP32, 4-8 MB flash), and the RAK4631 (nRF52840, 796 kB flash already at
69.5 %).

**The paper must also answer the cheapest alternative first:** MCProxy already speaks both node
protocols and runs on a Pi next to the node — a bridge on that side costs the firmware nothing.
Only if the answer is "on the node" does the rest of the analysis (which protocol per client, where
the serialiser lives, per-env flash/DRAM cost, unsupported platforms) become necessary.

#### DOC-01 — HTML timeline of the main loop with the confirmed stalls (point 9)

Deliverable: a self-contained HTML page in `docs/presentation/`, tab-switchable between
"upstream dev" and "this branch", vertical axis, empty/filled circles for non-blocking vs. blocking
calls, each with what it does, how long it blocks, and the LoRa-RX consequence.

**First correction for whoever builds it:** `src/loop_functions.cpp` is _not_ the main loop — it is a
5386-line support file. The loops are `esp32loop()` (`src/esp32/esp32_main.cpp:1846-3943`, 47 call
sites, `delay(5)` at `:3942`) and `nrf52loop()` (`src/nrf52/nrf52_main.cpp:1143-2514`, 37 call sites,
`delay(100)` at `:2511` plus `yield()`), dispatched from `src/main.cpp:53-75`. A stall on the RAK is
measured against a ~100 ms floor, not against zero.

**Instrumentation that produced the numbers:** `src/instrument.{h,cpp}` — `INSTR_LOOPTICK()` for the
loop period, `INSTR_SECTION(name)` for up to 16 named sections (9 wired on ESP32, 33 on nRF52), an
immediate report for any gap over 250 ms (`instrument.cpp:44`) naming the longest section of that
iteration, printed by `--instr`. On by default on ESP32/nRF52 (`instrument.h:29-35`) and explicitly
marked temporary scaffolding, not for upstream.

**Measured before/after, all sourced:** TM-01..04 audio 1552 → 23 ms; TM-08 idle invalidations
36.9 → 7.0/s; TM-09 OLED frame push 579 → 34.5 ms and loop max 645 → 39 ms; TM-11/TD-01 WiFi first
join 0/24 → 24/24 with `got_ip` median 55.8 → 14.2 s; TM-15 boot 17.8 → 4.6 s; TM-16 `SetupUBLOX`
1933 → 899 ms and ready 14.9 → 10.9 s over 24 boots; TM-20 no gap > 0.7 s, loop max 26 ms; TM-34 DNS
up to 31 s → 22-93 ms async plus the removed `esp_wifi_get_mode()` gaps of 2.9 s and 1.1 s; TM-35
`getUDP()` 1.6 s every ~20 s and loop max 1.7-3.4 s → 314 ms → 145 ms with 0 gaps > 250 ms; TM-45
0 NTP successes and 545-548 timeouts per board in 9.1 h → success with 89 ms RTT; BATT-01 ~100 ms
every 500 ms. Still open, no "after": TD-05/P1 — flush 36.7 ms mean, 184 flushes per 60 s ≈ 11.5 %
duty, loop max 72 ms at idle. Mark as estimate/unverified: G05 (~870 ms in the LVGL `read_cb`), G06
(`addMessage` 2 s, 8 s at boot), TM-30 (not reproduced).

**The RX-loss model must be split by platform, not averaged.** ESP32: the DIO1 ISR only sets
`receiveFlag` (`esp32_main.cpp:498-509`); the radio is re-armed only inside `checkRX()` (`:3946`,
`readData()` then `startReceive()` around `:4009`), which runs from the loop — so any packet arriving
after a completed RX_DONE while the loop is stalled is lost, and a stall before any packet costs
nothing. nRF52: `OnRxDone` runs on the radio library's own task and re-arms immediately
(`lora_functions.cpp:369`, `startRadioReceive()` `:435-439`) — _except_ while the W5100S owns the
shared SPI (`bSPI_ETH_Active` → `bPendingRadioRx`, drained in the loop). On the RAK the loss window
is therefore SPI contention with Ethernet, not a generic loop stall. **No experiment measures packets
lost per second of stall** — the page must not invent that number.

**The RAK "ETH link up, internet down" case the operator named** has no measured stall of its own.
The two nearest items: TM-44 (deferred by operator decision) — a 1.6 s loop block at the `ntp` site
while the WAN default gateway does not answer ARP, from the W5100S retry schedule; and ETH-01 (open)
— link poll, heartbeat and DHCP renew all nested inside `if(bGATEWAY)`. Anything beyond those two
belongs on the page as UNVERIFIED or not at all.

#### DOC-02 — `--help` renewal and a command markdown (point 10)

The parser is a single function, `commandAction()` (`src/command_functions.cpp:208-5977`), fed by
every channel (serial, BLE, net console, web, T-Deck UI, button, MCP frames) — `phone_commands.cpp`
is the binary BLE frame protocol, not a second parser. 292 live `commandCheck()` sites, ~230 distinct
verbs. The help text is `:726-827`.

**Two shipped bugs in the help text, worth fixing regardless of the rewrite:**

- `:762` prints `−−settime yyyy.mm.dd hh:mm:ss` with **U+2212 MINUS SIGN**, not ASCII `--`. Visually
  identical in most terminal fonts, but `commandAction()` requires `memcmp(sVar, "--", 2) == 0`
  (`:222`), so copy-pasting that line yields "wrong command".
- `:772-774` prints the T-Deck-only commands `--mute`, `--persistflash`, `--persistsd`,
  `--immediatesave`, `--persiststat` with **no `#ifdef`** — every board advertises them, and only
  T-Deck has them. Compare `--rotate` (`WP_DISP`-gated) and `--lps33` (`BOARD_RAK4630`-gated), which
  do it correctly.

**Missing from help** (roll-up; the full table is what the markdown has to carry): the entire bench
surface (~48 commands), the whole ping subsystem (`--pingcall`, `--pingtime`, `--ping start|stop`,
`--pingmax`), `--txsf` and `--txcr` (shipped, airtime-relevant, sitting right next to the documented
`--txpower`/`--txfreq`/`--txbw`), `--cleanflash` (the documented recovery path for a bricked settings
flash, per `docs/upstream-issue-harvest-20260829.md:49`), `--sendhey`/`--sendtele`/`--sendtrack`
(only `--sendpos` is listed), `--netmode`, `--relay`, `--gps autosymbol|fixsymbol`, `--via` and
`--viadebug`, `--debug csv|man|en|de`, `--setcont`, `--setlog`, `--setretx`, `--shortpath`,
`--softser baud|rxpin|txpin|app0`, `--aht20`, `--sht21`, the INA226 group, most of `--analog`,
`--batt factor`, `--board led`, `--tempoff`, `--setrtc`, `--setpress`, `--setublox`/`--setl76k`,
`--wifitxpower`, `--webtimer`, `--io`/`--setio`/`--setout`, `--aprsmc`. **Listed but nonexistent:**
`--SS on  use SS` (`:768`) — no matching `commandCheck` anywhere; a leftover from `83c0e3c9`.

**The clean-ship / developer split does not fall where it looks.** `INSTRUMENT_ENABLED`
(`src/instrument.h:29`) defaults to **1** on ESP32 and nRF52 and is never overridden in any
`platformio.ini` — so every shipping board build today contains the whole bench surface. The split
the operator wants is therefore either (a) documentation-only, keyed on "`INSTRUMENT_ENABLED`-gated"
regardless of the current default, or (b) preceded by a real release environment carrying
`-D INSTRUMENT_ENABLED=0`. Since the operator has explicitly **not** decided what ships clean, the
first version of the help and the markdown are written without the split, and the markdown records
the fault line so the split can be applied later. Fork-only and always-on: `--maxhop` (`CS-01`) is
the only one; everything else fork-only is inside the bench block.

Other things the markdown must record because they surprise readers: `commandCheck()` is a **prefix**
compare (`:127`), so `--setinet` also matches `--setinet off`, and the order of the dispatch chain is
load-bearing; `--help` over BLE is dead code (`:729-733`, the `else` is commented out), which is why
phone apps never show it; `--setowndns` has two identical handlers (`:3780` and `:3848`, the second
unreachable); `--setownms` (`:3822`) is missing the `else` that the rest of the chain uses; and three
`commandCheck` blocks are inside `/* */` and are not commands at all (`--compress` `:257-270`,
`--softser test0|test` `:3087-3110`, `--softser xml` `:3170-3178`).

**No test asserts any of this.** `command_functions.cpp` is in no `[env:native*]` build, and there is
no golden-output or list-consistency check — help and code are free to drift again. Existing docs are
not a substitute: `docs/command-changes-pr1102-1103.md` covers two merged upstream PRs only and its
line numbers have already drifted; `docs/loradebug-serial-output.md` documents log formats, not
commands; the README has nothing.

#### CONF-01 (enriched) and DOC-03 — nRF52 CONF apply, endianness and compatibility (point 11)

**Premise corrected.** TM-39 is a test/instrumentation item and is `DONE 2026-08-30`; the serial
command `--gateway srv OE|DL|IT` works correctly on **both** platforms (`command_functions.cpp:2233-2264`
— writes `meshcom_settings.node_gwsrv`, calls `save_settings()`, reboots). The defect the operator
described is the already-open `CONF-01`, whose row above stays the implementation line. What the
scouting adds to it:

The nRF52 `CONF` branch (`src/nrf52/nrf_eth.cpp:668-780`) parses correctly and then stops:
callsign into a local `String _longname` (`:730`), shortname (`:743`), lat/lon/alt (`:753`, `:761`,
`:769`). Grepping every later use of those variables in the file returns only those assignment lines
— no write to `meshcom_settings`, no `save_settings()`, no `rebootAuto`; the block ends at `:783`.
So: **parser present, apply missing, save missing, reboot missing.**

The reference to mirror is the ESP32 path, `src/udp_functions.cpp:533-586`, which the nRF52 side also
lacks the safeguards of: a source-IP guard so only the resolved server can provision (`:533`), a
`checkRegexCall()` on the callsign (`:566`), then `snprintf` into `node_call` (`:571`) and
`node_short` (`:575`), `save_settings()` (`:580`), and `rebootAuto = millis() + 15000` (`:585`, with
the T-Deck exception). The parser itself is `src/conf_frame.{h,cpp}` with 12 native cases
(`native_conf_frame`, `platformio.ini:315-329`). **No `FLASH_STRUCT_VERSION` bump is needed** — every
target field already exists in the struct, so no fleet wipe. There is no test for the apply step on
either platform.

**`DOC-03`** is the documentation half, and there is a concrete set of facts to write down:

- The CONF lat/lon/alt fields are **little-endian on the wire** — `nrf_eth.cpp:753-754` (byte 0 is
  the LSB) and the ESP32 parser deliberately matched to it (`conf_frame.cpp:61-62`, with the intent
  recorded in the comment at `:54-55`). Earlier documentation said big-endian; that was wrong.
- There is **no `htonl`/`ntohl`/`htons`/`ntohs` anywhere in `src/`** — zero hits repo-wide. Nothing
  is normalised to network byte order, and nothing needs to be: both MCUs are little-endian and the
  struct never crosses architectures.
- IP addresses are never stored packed — they live as dotted-quad ASCII (`node_ip[40]`, written at
  `udp_functions.cpp:1478`, `nrf_eth.cpp:719`) and are constructed per-octet on the wire
  (`IPAddress(145, 239, 75, 155)`), so there is no byte-order ambiguity. `node_gwsrv` is a 2-3 byte
  ASCII string; `node_country`/`node_alt` are native `int`s that are never serialised.
- The two platforms persist differently, and that is what actually constrains compatibility: ESP32
  writes each field as an NVS key/value (`esp32_flash.cpp:542-543`), so field order and padding are
  irrelevant there; nRF52 writes the **whole struct as a raw blob to LittleFS**
  (`nrf52_flash.cpp:391`), so on that platform the struct shape is load-bearing and a layout change
  without a `FLASH_STRUCT_VERSION` bump silently misreads old flash.
- The nRF52 side never resolves a hostname — every destination is a literal IP compiled in — while
  the ESP32 side resolves at connect time via async DNS. Together with `CTY-01` this belongs in the
  same document.

#### DOC-04 — reference markdown for the `config.json` registers (point 12)

Producer `configExportJson()` (`src/config_json.cpp:467`), importer `configImportJson()` (`:571`),
routes `GET /config.json` (`web_functions.cpp:407`, dispatch `:652`) and `POST /config` (`:471`,
dispatch `:658`); the X-macro table `CFG_FIELD_LIST` (`config_json.cpp:88-190`) drives **both**
directions, so it is the single backbone for the document. **107 keys on ESP32, 103 on nRF52**
(101 common, 6 ESP32-only, 2 nRF52-only).

Per key the markdown has to state: the C member, its type and array length, the import-time range
(`lo..hi`, or `CFG_NORANGE` = unvalidated — which is the case for every bitmask, the MCP17 IO config
and the group numbers), the stricter boot-time clamp where one exists, whether the field has a real
reader, and whether it carries a secret. The two validation layers disagree in a way worth calling
out: `node_bw` passes the importer with anything in `0..500`, but `sanitize_radio_params()`
(`settings_sanitize.cpp:63-78`) only accepts `{0, 125, 250, 500}` (or `{0, 1, 2}` on nRF52) and
silently corrects it on the next boot.

**The leftover the operator asked for, found:** exactly one key has no reader anywhere outside
export/import/flash — **`node_gpsbaud`**. GPS baud is auto-scanned via `GPS_BAUDS[]`
(`gps_functions.cpp:218, 221, 542, 640, 842, 844, 1109`), never from the setting. Deletion candidate.

Other facts the document must carry: four key/member name mismatches (`node_ssid`→`node_ossid`,
`node_pwd`→`node_opwd`, `node_lssid`→`node_ssid`, `node_lpwd`→`node_pwd`); **five** secret-bearing
fields, of which only three are documented in `config_json.h:40-44` (`node_lpwd`, `node_webpwd`,
`bt_code`) — the undocumented two are `node_pwd`/`node_opwd` (staging WiFi password) and
`node_passwd` (net-console password, masked on serial by `maskSecret()` but exported in clear); and
that the whole export is served with **no login at all** whenever `node_webpwd` is empty
(`web_functions.cpp:246`, `:306-307`), which is the `CS-03` finding. Also record the stale struct
comment: `max_hop_text` sits below the "nicht im Flash" boundary in both structs
(`esp32_flash.h:209`, `WisBlock-API.h:375`) yet is exported and has had an NVS key since `CS-01`.
Finally, what is deliberately _not_ exported and therefore not restored: the flash/firmware version
bookkeeping, `node_cleanflash`, the T-Deck UI block, the clock fields, and — by explicit gate
decision (`config_json.cpp:214-219`) — `node_msgid`/`node_ackid` and the live sensor readings.

Nothing in `docs/` covers this today; the only existing description is the comment block in
`src/config_json.h:1-141`, and `src/web_functions/Web-API_documentation.txt` documents the older
`/setparam/`/`/getparam/` API instead.

#### NTP-01 — cadence, report, `--ntpsync`, regression (point 13)

**(a) How often does it re-sync — 15 minutes, and not for the reason the code suggests.** The class
default `_intervalMs` is 900000 ms (`ntp_async.h:62`) and every caller overrides it to 3600000 ms
(`udp_functions.cpp:1241`, `nrf_eth.cpp:1185`, `:1312`) — but that hour never governs, because the
15-minute caller gate (`esp32_main.cpp:2633`, `nrf52_main.cpp:1219`) calls `udpUpdateTimeClient()`,
which unconditionally calls `timeClient.requestNow()` (`udp_functions.cpp:1434-1435`,
`nrf_eth.cpp:1200-1201`), setting `_nextDueMs = millis()`. The effective interval is 15 minutes,
driven from outside; the 1 h setting is dead. That contradiction is itself worth resolving.

Retry policy (`ntp_async.cpp:3-6`, state machine `:67-104`): reply timeout 2500 ms, retry after
5000 ms while fewer than 3 failures, 60000 ms from the third on — a fixed two-tier backoff, not
exponential; `_fails` saturates at 0xFFFF and only resets on success, `begin()` or
`setPoolServerIP()`. A kiss-of-death reply (stratum 0) forces the 60 s backoff and is consumed rather
than handed to the MeshCom parser. The class has no link awareness; the caller gates on
`node_hasIPaddress`, so with no IP the state machine is simply frozen. Both platforms use the same
class — there is no separate nRF52 implementation.

**(b) The report.** The old client was the stock `NTPClient` library (`platformio.ini:65`, still in
`lib_deps` although unused). It blocked the caller for up to 1 s per refresh, and `forceUpdate()`
drained the shared gateway socket, so a refresh could eat queued `GATE`/`CONF` datagrams. Replaced by
commit `50528168` (2026-08-30 13:00:53 +0200). Evidence to cite: `docs/BACKLOG.md:946` — "RAK 600 s
steady state: loop max 145 ms, 0 gaps > 250 ms (was 314 ms / 1 gap)" — with the artifacts
`tools/bench/runs/rak_instr600_ntpasync_20260830.json` and `…_b_….json`. `upstream/dev` (HEAD
`2cb6bb4d`, 2026-08-28) still runs the blocking client. Follow-ons to mention: TM-45 (fixed,
`81cfc064`) and TM-44 (deferred). The report should also name the duplication introduced by the
WEB-01 fix: `getEffectiveNtpServer()` (`web_functions.cpp:1097-1110`) re-implements the selection
that really lives in `udp_functions.cpp:1515-1638`, and the comment there says so. And it should
state who owns wall-clock time: `MyClock` (`src/clock.cpp:474`) is the single source of truth, fed by
GPS > RTC > NTP, with NTP additionally disciplining the DS3231/PCF8563 on ESP32
(`esp32_main.cpp:2688`, `:2755`).

**(c) `--ntpsync`.** Call the already-public `NtpAsync::requestNow()` (`ntp_async.h:42`) on the
global `timeClient` (`udp_functions.cpp:75` on ESP32, `nrf_eth.cpp:28` on nRF52) — both are
file-scope globals reachable by `extern`, exactly like the existing `extern IPAddress bench_srvip`
bench hook, whose command block (`command_functions.cpp:4996-5023`) is the template down to the
fork-only comment style. **Caveat pinned by an existing test**
(`test/test_ntp_async/test_main.cpp:212-237`): `requestNow()` does not cancel an in-flight request —
if one is outstanding the new due time only takes effect after the ≤2.5 s timeout. The command must
either report "already syncing" or document the wait.

**(d) Regression.** Native already covers the class: `test_ntp_async` (10 cases) and
`test_ntp_harvest` (4) under `[env:native]` (`platformio.ini:199`). Nothing covers the serial command
parser at all, on any command. On the bench, **no scenario parses `[NTP];…` markers** — the TM-35 and
TM-45 proofs were hand-grepped from raw logs. Add a `scenario_ntp` to `tools/bench/rak_harness.py`
for the RAK plus an ESP32-side equivalent (there is no shared harness — ESP32 boards use
`tdeck_harness.py`/`soak_harness.py`), driving `--ntpsync` and asserting on
`[NTP];ok|timeout|txfail|kod`, documented in the shape of `docs/bench-extudp-regression.md`, results
in `tools/bench/runs/`.

#### E22-01 — frame integrity under supply spikes and RF ingress (point 14)

**Premise corrected on three counts, and the operator's own observation is the load-bearing
evidence.** There is no field report of this in the repository — the observation is the operator's:
corrupted packets received over LoRa from E22 nodes with power-supply problems, and corrupted frames
arriving from the central server that originated at such a node. Mechanism per the operator: voltage
spikes and RF ingress into the regulator control loop flip bits; the send buffer is damaged.
Explicitly **not** trackable through the battery reading.

What the tree actually contains, all verified:

1. `src/esp32/pa_control.cpp` is gated `BOARD_HELTEC_V4 && HELTEC_V4_HAS_PA` (`:9`) — it compiles to
   nothing on every E22 environment. E22 boards drive the module's PA through RadioLib's RF switch:
   `radio.setRfSwitchPins(E22_RXEN, E22_TXEN)` (`esp32_main.cpp:1163-1164`).
2. **There is no low-voltage TX inhibit anywhere in the tree, for any board.** The only voltage
   trigger that ever existed (`src/batt_functions.cpp:417-452`) sent the _whole node_ to
   `esp_deep_sleep_start()` at `BAT_MIN_VOLTAGE`, and it has been commented out since upstream commit
   `e0043a56` (2026-07-11, "TBEAM 1W deepsleep deactivated", upstream #1053). The only E22-specific
   voltage code that is live is display cosmetics (`batt_functions.cpp:398-400`, `BatVoltage < 3.0 →
0`).
3. The brown-out detector is at the Arduino-ESP32 default (enabled) — no `BROWNOUT` or `sdkconfig`
   override exists anywhere in the repo — and **nothing in `src/` reacts to it**, a gap already
   recorded in `docs/review/2026-07-31/f7-testaudit.md:737`.

**What the frame already protects, and what that implies.** Two independent checks sit between a
sender and a receiver that would _display_ garbage:

- The LoRa PHY CRC is on (`rf_crc = true`, `lora_setchip.cpp:58`; `radio.setCRC(icrc)` at `:806`).
- The MeshCom frame carries its own FCS: a 16-bit **sum over all preceding bytes**, computed at
  encode (`aprs_functions.cpp:1100-1111`) and verified at decode (`:417-443`), with a hard
  `return 0x00` — frame discarded — on mismatch. The callsign comparison at `:430` only suppresses
  the debug print, not the discard.

So a corrupted frame that a peer or the central server actually _accepted_ passed both. That is the
single most useful analytic constraint for the concept: corruption occurring after `encodeAPRS()` —
in the ring, in the `memcpy` to `lora_tx_buffer`, or on the SPI write — would fail the FCS with
probability ≈ 1 − 2⁻¹⁶ and be dropped by the receiver. Garbage that survives to the map therefore
most likely entered **before or during** `encodeAPRS()`, i.e. in `aprsmsg`, in the source buffers, or
in the settings the frame is built from (the callsign among them) — or the FCS was computed over
already-damaged bytes. The concept has to test that hypothesis before it proposes a guard, because it
decides where the guard belongs.

**The chain, for reference:** `ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5]` (`txring_functions.cpp:27`) →
`memcpy` into `lora_tx_buffer` (`lora_functions.cpp:104`, copy at `:1517`) →
`radio.startTransmit(lora_tx_buffer, sendlng)` (`:1730`). The only check between them is a length
clamp (`:1513-1514`). No CRC, no canary: `crc32_util.h` exists but is used solely by `config_json`,
and the dedup ring keys on `msg_id`, not on content. A `startTransmit()` failure is rolled back and
requeued (`:1731-1740`), so driver-reported failures are handled — silent RAM damage is not. On
ESP32 every writer and reader of these buffers runs in `loopTask` (`src/ring_index.h:1-31`), so a
data race is not the mechanism either.

**The UDP question, answered:** the gateway's upload to the central server is built from `RcvBuffer`,
i.e. from what the node _received_ (`lora_functions.cpp:502` → `addNodeData()` `:1252` →
`ringBufferUDPout`), not from `lora_tx_buffer`. A corrupted transmit buffer does not, by this code
path, also reach the server from the same node — it can only come back via a peer that heard the
garbage and forwarded it. That matches the operator's second observation.

**Concept to be written and reviewed before any code** (the operator reviews and releases it):

1. Instrument first, because the mechanism is bit flips, not a threshold: a checksum or canary
   computed at frame build and re-verified immediately before `startTransmit()`, and a second one
   over the settings the frame is built from, so the concept can say _where_ the flip happened.
2. Extend the FCS-mismatch path (`aprs_functions.cpp:428-443`) into a counter and a marker, so
   damaged frames are countable in the field instead of only printed under `--loradebug`.
3. Decide what a node does when the check fires: drop the frame, requeue it, rebuild it from
   `aprsmsg`, or refuse to transmit until the next clean build — plus what it reports.
4. Separately, the questions upstream leaves open: whether to react to the brown-out detector at all,
   and whether to restore the deep-sleep guard `e0043a56` removed with a board-correct threshold
   (upstream #962 open, plus #1053, #910, #940 — `docs/upstream-issue-harvest-20260829.md:205-210`).
5. Hardware side, since the cause is supply-borne: the concept should say what is expected of the
   power supply (decoupling, brown-out margin at TX current peaks) rather than treating it purely as
   a firmware defect.

Adjacent and already fixed, for contrast, not as the same defect: upstream #661 → `TM-32`
(`docs/BACKLOG.md:943`, fixed 2026-08-29) — a brown-out corrupted the _stored settings_ on a RAK4631
and the firmware applied out-of-range radio parameters on boot; `src/settings_sanitize.{h,cpp}` now
range-checks them. Different board family, different memory, different symptom — but the same root
cause class, and the reason a settings-side canary belongs in point 1 above.

---

### 3.8q Upstream sync 2026-09-01 — PRs #1115, #1117, #1118 merged back

Fetched 2026-09-01: `upstream/dev` = `898ff7e5`, three merges past our previous base
`2cb6bb4d` — our firmware-only cut (#1115, merged 14:54Z), Kurt's `v4.35p compile`
(#1117, `72fce319`), and our upload-port fix (#1118, merged 18:18Z, `5661dd69`).
Merged into fork main as `8beaa8e5`; four conflicts, resolution in the merge commit
message (three `platformio.ini` hunks and `variants/T-ETH-ELITE_1262/platformio.ini`
to the upstream side — they are our own #1118; `docs/code-audit-20260508.md`
rename/delete to ours). Gate: native 148/148, seven standard targets build.

| ID    | Where                            | Finding                                                                                                                                                                                                                              | Sev. | Action                                                                                                 |
| ----- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---- | ------------------------------------------------------------------------------------------------------ |
| UP-07 | `variants/*/platformio.ini` (9)  | `--port "$UPLOAD_PORT"` stands in **27** files here but only **18** upstream. #1117 landed a variant set that predates the full fix, so the fork is ahead on the remaining 9.                                                        | Low  | **Hold.** Do not PR before the `--port` question below is settled. See UP-08.                          |
| UP-08 | `upload_command` in all variants | **Operator report 2026-09-01: `--port "$UPLOAD_PORT"` caused problems.** The concrete failure mode is not yet written down here. Until it is, the 27-file state is not established as correct — it is merely what fork main carries. | Open | Capture the failing invocation (board, host, `pio` command, error text) before any upstream follow-up. |

**Why UP-07 is parked rather than filed as a PR.** Upstream is expected to move over the
next days. Re-check the delta then instead of pushing a 9-file variant PR now: if Kurt's
next `v4.35p compile` pass touches the same files, the diff resolves itself, and if UP-08
turns out to be real the fork side is the one that needs changing, not upstream's.

**Process reminder (from §3.8g):** the net diff since the last merge base gets reviewed at
merge time, findings filed as `UP-nn` here. This sync's net diff is build configuration
only — no `src/` change arrived — so no new native test was owed.

### 3.8r GPS/NMEA link is structurally lossy — field report `OE5HWN-14` (2026-09-01)

Full analysis, with every log excerpt the claims rest on:
[`bug-GPS-uart-overflow-20260901.md`](bug-GPS-uart-overflow-20260901.md).

**Intake.** OE5HWN reported that the WX dashboard shows `OE5HWN-14` (T-Beam Supreme, 4.35p)
wandering between 179 m and 308 m of altitude while the node never moved. Two `--gpsdebug`
captures were supplied. The reported symptom is real but is the smallest part of what the logs
contain.

**Root cause.** `WZ_GPS_Loop()` is the only code that drains the GPS UART, and on ESP32 it runs
once every 3 s (`GPS_REFRESH_INTERVAL 3`). The L76K sends GGA+RMC at 1 Hz ≈ 140 B/s into the
256-byte Arduino default ring, so ~165 bytes are discarded by the ISR **every cycle**, on every
stationary ESP32 node, permanently. The cut lands mid-sentence; the parser resyncs at the next
`$` and splices the remains onto a later sentence. The 8-bit NMEA checksum catches almost all of
it — but ~1 in 256 passes by chance and is committed as a real fix. Predicted ~1.7 false commits
in the 22-minute log, observed exactly 1 (`lon:0.000000`, `Date: 2015.14.00`, with `fix:yes
sat:7 hdop:2.7`). The nRF52 path in the same tree already polls at 1 s and does not overflow.

**Not ours.** `git diff upstream/dev HEAD` over the whole GPS/altitude/QNH path is empty; every
construct blames to Kurt, 2023-03-05 through 2026-04-23. Detail in the doc's §9.

| ID      | Where                                                                        | Finding                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | Sev.   | Action                                                                                                                                                                  |
| ------- | ---------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GPS-01  | `esp32_main.cpp:3079`, `configuration_global.h:178`, `gps_functions.cpp:889` | **IMPLEMENTED 2026-09-02 on `feat-gps-nmea-20260902` (worktree `mc-gps`):** `WZ_GPS_Feed()` drains the UART every loop pass, `WZ_GPS_Loop()` evaluates on its own timer (§3.1). Review verdict `docs/review-verdict-gps-20260902.md`; bench (DK5EN-14 arms A/B/C) pending — node not attached. Was: GPS UART drained only every 3 s; 420 B of NMEA into a 256 B ring → ~165 B lost per cycle, always. nRF52 already polls at 1 s.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | High   | Split `WZ_GPS_Feed()` (drain, every loop) from the evaluation (every 3 s). **Not** `setRxBufferSize` — see below.                                                       |
| GPS-02  | `gps_functions.cpp:919`, `:959`, `:1009-1024`                                | **IMPLEMENTED 2026-09-02 on `feat-gps-nmea-20260902` (worktree `mc-gps`):** `gpsSamplePlausible()` gates null island, angle, altitude range and impossible calendar dates; `gpsTimePlausible()`/`gpsDatePlausible()` (review fix F1) also guard both clock-write call sites; rejects are counted and logged (`reject:` line). Review verdict `docs/review-verdict-gps-20260902.md`; bench (DK5EN-14 arms A/B/C) pending — node not attached. Was: No plausibility gate. `gpsData.valid` written, never read; the fix gate checks only sat/HDOP, which a damaged RMC leaves intact. Corrupt sample lands in persistent settings verbatim.                                                                                                                                                                                                                                                                                                                                                                                                   | High   | Gate on `isValid()` + `age()`; reject lat/lon exactly 0.0 and impossible dates.                                                                                         |
| GPS-03  | `gps_functions.cpp:1022`, `command_functions.cpp:4131`                       | **IMPLEMENTED 2026-09-02 on `feat-gps-nmea-20260902` (worktree `mc-gps`):** scalar Kalman filter (`AltFilter`, dt-scaled process noise via `ALT_KF_DT_REF_MS`, review fix F2) replaces the raw sample; `--setalt` seeds it and now rejects out-of-range input instead of clamping to 0 (F7). Review verdict `docs/review-verdict-gps-20260902.md`; bench (DK5EN-14 arms A/B/C) pending — node not attached. Was: `node_alt` is one raw unfiltered sample, beaconed every 30 min. `--setalt` is silently overwritten by the next fix — the command is a no-op on any node with a GPS.                                                                                                                                                                                                                                                                                                                                                                                                                                                       | Medium | Scalar Kalman, constant `R`, `q`≈0.01 (τ≈400 s) — 8 B, measured 4.36→1.52 m RMS. **Not** a 5-sample median: that measures 2–5 %. Make `--setalt` sticky, then `--help`. |
| GPS-04  | `bmx280.cpp:326` (+ `bme680.cpp`, `bmp390.cpp`)                              | **IMPLEMENTED 2026-09-02 on `feat-gps-nmea-20260902` (worktree `mc-gps`):** `baroBaseRelatch()`/`baroBaseLatchAllowed()` re-latch QNH on filter convergence and on `--setalt`/`--setpress` (F9); the latch is withheld only while a fix currently exists and the filter has not converged (F4), so a node that never fixes still latches its persisted altitude as before. Review verdict `docs/review-verdict-gps-20260902.md`; bench (DK5EN-14 arms A/B/C) pending — node not attached. Was: QNH reference altitude latched to the first fix after boot, never corrected. An outlier at boot costs ~±7 hPa for the whole session — and this node rebooted twice in 7 minutes.                                                                                                                                                                                                                                                                                                                                                            | Medium | Re-latch on `--setalt` and on the GPS-03 filter's convergence signal (`P` below threshold) — not on the first fix.                                                      |
| GPS-06  | `gps_functions.cpp` (`detectBaudrate()`), `variants/t_deck/configuration.h`  | **IMPLEMENTED 2026-09-02 on `feat-gps-nmea-20260902` (`79037fd5`), field finding OE5HWN:** T-Deck without Plus, self-wired module: fix on 4.35d, silent on 4.35p. Upstream `a672d18b` swapped the pin defines to the LilyGo assignment (`RX 44 / TX 43`), the reverse of 4.35d's `SoftwareSerial(43, 44)` on this board. `detectBaudrate()` re-scans once on the pre-4.35p pins when the variant defines `GPS_FALLBACK_RX_PIN`/`GPS_FALLBACK_TX_PIN` (only `variants/t_deck`), keeps the effective pins, logs a request to swap the wires. Plan §3.7.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | Medium | Release note for upstream: "T-Deck ohne Plus: Modul-TX an GPIO44, Modul-RX an GPIO43, wie beim Plus".                                                                   |
| GPS-07  | `gps_filter.h` (`ALT_KF_RESEED_N`, `ALT_KF_GATE_M`)                          | **OPEN, found 2026-09-03 in the overnight capture of `DK5EN-14` (9791 evaluations, 0 rejects, 0 corrupt samples):** the re-seed guard (gate 15 m, 10 consecutive rejects = 30 s at the 3 s ESP32 cadence) fires inside ordinary altitude excursions of a stationary receiver, which last 60-120 s (the filter's own comment) and reached +36 m; it re-seeded the estimate 10 times in 8 h onto a single raw outlier (02:48: 493.9 -> 518.1 m, beaconed as 507 m on a ~487 m node). An offline replay of `gps_filter.cpp` reproduces every beaconed `/A=` within 1 m, so the filter runs as designed and only the guard is mis-sized. Replay: `ALT_KF_RESEED_N` 60 (3 min) or `ALT_KF_GATE_M` 30 -> 0 re-seeds; error at the 16 beacon times: raw sample mean 7.6 m / worst 18.0 m, shipped filter 3.8 / 18.5 m, with RESEED_N 60 3.2 / 10.1 m. hdop-weighted `R` adds nothing (every hdop bucket ~4.5 m median error); leave `Q`. Data and replay scripts: `~/Downloads/meshlog-20260903/` (`14.log`, `kf.py`, `kf2.py`, not in the repo). | Medium | One constant, rerun `test_gps_filter`, validate on the capture. Still better than raw even unfixed.                                                                     |
| TM-51   | boot path, both platforms                                                    | **Fixed 2026-09-02 (`e108fa98`):** ESP32 prints `[BOOT] RESET_REASON=<n> <name>` right after `CLIENT SETUP` (raw `Serial.printf`, survives `--debug off`); nRF52 has printed `[BOOT] RESETREAS=` since 2026-08-21. Was: no reset reason is ever printed (`grep esp_reset_reason src/` → empty). `OE5HWN-14` reset at ~20:31:58 (proven three ways: `millis()` rollback, min-free-heap watermark rise, 32 s hole) and we cannot say why.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | Medium | **Do this first.** Three lines: `esp_reset_reason()` / `NRF_POWER->RESETREAS` in the boot banner.                                                                       |
| TM-52   | `esp32_main.cpp:1844`, `:3365`                                               | **DROPPED from the 2026-09-02 campaign (operator).** Probable cause recorded in `gps-nmea-impl-plan-20260902.md` §9 (Supreme uses SW-I2C 1-page U8G2 ctor). Was: Display section measures ~570 ms per update on the T-Beam Supreme, 39x in 17 min. Two fire at 7.5 s uptime, so it is not LoRa airtime. `setBusClock(400000)` is Heltec-only; the 100 kHz arithmetic is still 3x short of 570 ms.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Medium | Direct measurement inside the display path (sections around `clearDisplay` / `nextPage`), not more reading.                                                             |
| GPS-05a | design item, no single site                                                  | Moving-node altitude, boards **without** pressure sensor: innovation gate + vertical-rate limit + speed-adaptive `q` (`gpsData.speed_kmh` is already parsed). Runs everywhere. The stationary case needs none of it — §7.4's filter is already GPS-only.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | Low    | Use `bDisplayTrack` as the discriminator first; speed-adaptation is a refinement needing field validation. Doc §7.6.2.                                                  |
| GPS-05b | design item, no single site                                                  | Moving-node altitude, boards **with** pressure sensor: baro/GPS complementary filter, mirroring mcmap's server-side `altSmoothed`. An upgrade, never a prerequisite. Runtime discriminator exists: `bBMPON`/`bBMEON`/`bBME680ON`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Low    | Do not start before GPS-01 lands and TRACK-mode logs with pressure exist. Doc §7.6.                                                                                     |

**Kalman-filter question (review, 2026-09-01) — answered with measurements, not argument.** The
proposal was an HDOP-weighted Kalman filter on the altitude. **Rejected as specified**, on four
counts, all measured against both field logs (doc §7.6): (1) HDOP is a weak predictor of the
vertical error — `r(HDOP, |err|)` is +0.51 in one session and +0.20 in the other, and it is the
wrong DOP anyway (vertical error scales with VDOP, which the node never receives because its own
`$PCAS03` disables GSA — and enabling GSA adds ~70 B/s, making GPS-01 worse); (2) the HDOP term
contributes nothing — the same filter with `R` fixed at the session median scores 1.57/1.97 m RMS
against 1.59/1.95 m HDOP-weighted, i.e. identical to the third digit; (3) the dominant error is
time-correlated, not white — autocorrelation is +0.98 at 3 s and only decays after 60–120 s, so a
white-noise `R` makes the filter overconfident by construction; (4) an **8-byte filter with τ ≈ 400 s** matches or beats it (1.52/1.62 m RMS, worst sample 25.4 m → 2.6 m) with no tuning and
no array. Above all: the two sessions' medians differ by **4.6 m** at the same antenna twenty
minutes apart, so a within-session estimator improving scatter from 1.6 m to 1.5 m is invisible in
the delivered quantity. **This also corrected GPS-03's own recommendation** — the first draft
proposed a median over 5 fixes (15 s), which measures out at 2–5 % because the error is still
~80 % correlated at 15 s. The window has to be minutes.

**"And if the node only has a GPS?"** Nothing changes for the stationary case: **every measurement in §7.6 and §7.4 was computed from GPS altitude alone** — both field logs are GPS-only altitude series, no barometer was used anywhere. The 1.52/1.62 m result _is_ the GPS-only result. GPS-05b (baro fusion) is an upgrade for boards that carry a sensor, never a prerequisite, and the firmware already knows which case it is in at runtime (`bBMPON`/`bBMEON`/`bBME680ON`, `loop_functions_extern.h:90-95`). What stays genuinely hard is a **moving** node with no barometer — filed as GPS-05a, three sensor-free mechanisms (innovation gate, vertical-rate limit, speed-adaptive `q`). Honest limit stated in the doc: a correlated error cannot be averaged out while the true value is also changing, so outlier rejection and a rate bound are the deliverables there, not metre-level altitude.

**Why not `setRxBufferSize(1024)`.** It would mask GPS-01 for 1 kB of static RAM per GPS board.
Rejected on operator decision 2026-09-01: the RAM budget is the binding constraint on this tree
(`MEM-01` guard, `MEM-02` parked, E22-DevKitC at ~1.7 kB DRAM headroom per `CS-03`). It also
would not fix GPS-02 — a bigger buffer still delivers spliced sentences once the loop stalls
long enough. The polling split costs zero RAM.

**Cheapest falsification, before any code:** run a bench node in TRACK mode (`gps_refresh_intervall`
= 1.0 s, `esp32_main.cpp:3076`) with `--gpsdebug 1` for two hours. The model predicts near-zero
corrupt samples versus ~5 at the 3 s cadence. If TRACK shows the same rate, §4 of the doc is wrong
and the GNSS module is the suspect instead.

**Retraction carried in the doc (§4.3):** the first reading blamed the ~580 ms loop stalls, using
the 38400 line rate instead of the 140 B/s data rate. At the real rate the ring holds 1.8 s and a
580 ms stall does not overflow it — and there is no stall within five minutes of the corrupted
sample. The stalls are a separate defect (TM-52).

**Upstream:** GPS-01/GPS-02 are good PR candidates (small, platform-symmetric, affect every ESP32
node in the network). GPS-03/GPS-04 change `--setalt` semantics and go through §3.5 — propose as a
plan first. GPS-05a/GPS-05b are not upstream-ready and must not be offered until they have evidence.

### 3.8s Fixed IP loses the MeshCom server, and the web GUI cannot show why — operator report (2026-09-01)

Full analysis, with every code site the claims rest on:
[`bug-static-ip-dns-20260901.md`](bug-static-ip-dns-20260901.md).

**Intake.** Chat thread, E22 node: "with a fixed IP I have no connection to the server, with DHCP
it works without problems; in both cases I can reach the node's web server without problems", and
on being asked again about the web interface, "yes, that's the odd part, but it feels sluggish".
Netmask, gateway and DNS were reported as identical in both modes.

**Root cause.** Switching to a fixed IP silently swaps the resolver. The DHCP branch takes
`WiFi.dnsIP()` from the router; the static branch takes `node_owndns`, and if that string is
shorter than seven characters it substitutes the hard-coded literal **`8.8.8.8`**
(`udp_functions.cpp:1494-1497`, repeated at `:1508-1511`). The MeshCom server is reached by name
(`meshcom.oevsv.at`), so a resolver that cannot answer leaves `node_hostip` at `0.0.0.0` and the
node emits **no** server traffic at all, while the local web server — which needs no DNS — keeps
working. `node_owndns` is consulted **only** in static mode, and the mode gate at `:1489` does not
require a DNS at all.

**Why the operator could not see it (NET-01, the item that produced this ticket).** The four "IP
Network Settings" boxes are pre-filled from the node's **effective** values while their buttons
write the **stored** ones — `web_functions.cpp:1432-1435`: `node_ip`/`node_subnet`/`node_gw`/`node_dns`
in, `setownip`/`setownms`/`setowngw`/`setowndns` out. NTP at `:1436` is the only one of the five
that shows the stored setting, added in the same commit as the wrong DNS box. So under DHCP the
DNS box shows the router's address although `node_owndns` is empty, and "it says the same in both
variants" is not evidence of anything. `/getparam/` returns the stored values (`web_setup.cpp:880-901`),
i.e. page and API disagree — and the page never calls the API, which is the same blind spot CS-04
was hiding behind (`web_functions.cpp:2333-2335`).

**HAMNET corollary.** On 44.x the server name is chosen by `node_gwsrv` (`udp_functions.cpp:1581-1631`):
OE gets the literal `44.143.8.143` and survives, `DL` gets `meshcom.hamnet.cloud` and `IT` gets
`meshcom.dig-italia.it` and do not. A DL or IT HAMNET gateway on a fixed IP without OWNDNS is
handed a resolver that is unreachable from HAMNET by definition **and** a server name it cannot
resolve.

**Not ours.** `git diff upstream/dev HEAD` over both files is empty; blame is Kurt (2025-01-15 …
2026-06-09) and Luca Cireddu (2026-06-09). NET-05 is the single item this branch already fixed.

| ID     | Where                                                                                                   | Finding                                                                                                                                                                                                                                             | Sev.   | Action                                                                                                          |
| ------ | ------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------------------------------------------------- |
| NET-01 | `web_functions.cpp:1432-1435`                                                                           | Setup page shows effective network values in the boxes that write the stored ones; NTP (`:1436`) is the only correct one. The GUI can never show whether a static setting exists.                                                                   | High   | Pass `node_own*` as `inputValue`, as NTP already does. **Do this first** — it makes every other fix verifiable. |
| NET-02 | `udp_functions.cpp:1494-1497`, `:1508-1511`                                                             | Empty `node_owndns` on a fixed IP falls back to `8.8.8.8` without a word in the log. Fails on any site that blocks, hijacks or cannot route public DNS — and categorically on HAMNET.                                                               | High   | Fall back to `node_owngw` (already configured and validated), and log the substitution.                         |
| NET-03 | `udp_functions.cpp:1522-1525`, `:1548`, `:1555-1562`                                                    | On a fixed IP `hasIPaddress` is derived from the settings string, not from the interface: a failed `WiFi.config()` is printed and ignored. `--info` reports `hasIpAddress: yes` (`command_functions.cpp:5919`), KEEP goes to `0.0.0.0`.             | Medium | Read `WiFi.localIP()` back after `WiFi.config()`; treat a `false` return as a bring-up failure.                 |
| NET-04 | `command_functions.cpp:3921/3926`, `:3961`, `:3989/3994`; `udp_functions.cpp:1494-1497` vs `:1508-1511` | Two `--setowndns` handlers — the live one (`:3926`, offset `+12`) correct, the dead duplicate (`:3994`, offset `+11`) would have cut the first character off the address. `:3961` is missing its `else`. DNS block duplicated in the static branch. | Low    | Delete the dead handler and the duplicate block, restore the `else`.                                            |
| NET-05 | released builds only, pre-`f34fd2ae`                                                                    | Server and NTP resolved with blocking `WiFi.hostByName()` (2 calls, up to 31 s each, in the loop task) and no retry after a failure — permanent until reboot. The "sluggish" half of the report.                                                    | Medium | **Already fixed here and upstream** (`f34fd2ae`, TM-34 Wave W). Nothing owed but a release.                     |
| NET-06 | `udp_functions.cpp:1215-1246`                                                                           | The static address is applied only from the `got_ip` path, i.e. after a DHCP lease was obtained. On a network without a DHCP server a static node never applies its configuration.                                                                  | Low    | `WiFi.config()` before `WiFi.begin()` when a static configuration is stored. Needs bench proof first.           |

**Confirmation still owed, on the reporter's node.** Two commands, in order: `--info` → the
`...OWNDNS address:` line (`command_functions.cpp:5902-5908`) — empty confirms the mechanism; then
the boot log after `[WIFI]...Internet UDP-DEST meshcom.oevsv.at` → `[WIFI];dns;…;ip;0.0.0.0` proves
it is resolution and nothing else. A valid address there kills the hypothesis and sends the search
to the two alternatives kept open in the doc's §8: a stored resolver that only answers the
DHCP-assigned address (per-client access profiles, guest VLAN, Pi-hole ACL), or a duplicate address
from a static IP inside the DHCP pool — the latter explains "sluggish" equally well, and `ping` with
the node powered off settles it.

**Operator workaround, independent of any firmware change:** `--setowndns <router IP>` + `--reboot`,
or the same value in the GUI's DNS box **with the button pressed**, even when the box already shows
the right address. Lower-risk alternative: stay on DHCP and pin the address by MAC in the router.

**Tests owed with the fix** (`env:native_config`): empty `node_owndns` resolves to the gateway, not
`8.8.8.8`; `--setowndns 1.2.3.4` stores `1.2.3.4`, not `.2.3.4` (pins the offset the dead handler got
wrong); the rendered setup page carries the stored `node_own*`. Bench proof for NET-02/03 on
`DK5EN-93`: fixed IP without OWNDNS, port 53 to `8.8.8.8` blocked at the AP, expect
`[WIFI];dns;meshcom.oevsv.at;ip;0.0.0.0` and no KEEP; after the fix, resolution via the gateway and
KEEP resuming.

**Upstream:** NET-01 + NET-02 are good PR candidates — two files, a handful of lines, no behaviour
change for any node that is not statically addressed, and they close a class of report rather than
one node. NET-03 changes what `hasIpAddress` means and goes through §3.5 as a plan first. NET-04
folds into either. NET-06 must not be offered before it has bench evidence.

### 3.8t Code Quality 2.0 harvest — items from the session mining and the CQ2 scanner (2026-09-02)

Source: [`code-quality-2.0.md`](code-quality-2.0.md) (28 code-defect and 16 process patterns mined
from the sessions 2026-08-27..09-01, the fix wave since 2026-08-18 and the review docs) and the
first `--full` run of the extended `tools/code_audit_scan.py` (rules `CQ2-*`, commit `e108fa98`).
Four sites the new checks found were fixed in the same commit (map-timer `millis()` form, ESP32
`RESET_REASON` banner = TM-51, `PositionToAPRS()` `strncat` bound, sequential builds in the
build skill). Everything below is what the checks found and did **not** fix, plus the enablement
work the patterns call for. Already filed elsewhere and not repeated here: the four variants that
still pair `upload_protocol = custom` with `$UPLOAD_PORT` (UP-07/UP-08), the callsign regex
(UP-06), `sendHey()` rate floor (FL-02), the nRF52 timer-task flash write on the DM ACK path
(§3.8l / advisor-dm M5).

Priority = Sev. column. **High** blocks a regression test or leaks a credential; **Medium** is a
latent defect or a gate the next wave needs; **Low** is hygiene.

| ID    | Where                                                                                                                                                  | Finding                                                                                                                                                                                                                                                                                                                                                                                                                         | Sev.   | Action                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CQ-01 | `command_functions.cpp` (`--info`, `...BTCODE %06i`), `esp32/esp32_main.cpp` (`Server PassKeyRequest <%06i>`)                                          | The BLE pairing PIN is printed in clear on serial and on the unauthenticated 2323 console. N-31 masked `node_pwd`/`node_webpwd`/`node_passwd` and missed `bt_code`. Found by `CQ2-C14` (pattern C14). The PIN is the only thing between a phone in radio range and the command channel (N-07).                                                                                                                                  | High   | `maskSecret()` on both sites (or drop the PassKeyRequest print, it is a debug leftover). Bench: `--info` over 2323 must not contain the PIN. One-line change, no test env needed; grep `CQ2-C14` must return 0.                                                                                                                                                                                                                                                                     |
| CQ-02 | `loop_functions.cpp` (`aprsmsg.msg_id = millis();` in the response/echo frame builder, ~`:666`)                                                        | Response frames mint `msg_id` from `millis()`: two frames in one millisecond share an id (the app dedups them), and `0` at rollover is "no id" for `checkOwnTx()`. BP-07/BP-10 M8 fixed exactly this for the BP notice frames with `bpNextMsgId()`; this producer was not converted. Found by `CQ2-C12c` (C12).                                                                                                                 | Medium | Route through the monotonic allocator (rename `bpNextMsgId()` to a general `nextLocalMsgId()` or keep both names); native test: two calls in the same `now_ms` yield different ids, never 0. Pinned in `test_bp_notice_frame` style.                                                                                                                                                                                                                                                |
| CQ-03 | `t-deck/lv_obj_functions.cpp` (`save_/load_persisted_messages()`, `persisted_msgs`), `t-deck/tdeck_main.cpp` (`log_message_to_sd()`), GUI switch HL-04 | `HEAP_TEST` and `T_DECK_SPIFFS` are defined by no env (`pio project metadata`, C07 detector). SD/SPIFFS message persistence is compiled out, the "persist to SD" switch drives dead code, and `persisted_msgs` is a write-only 1000-entry `String` vector on the internal heap (the G04 finding). A session once reported "message survived the reboot via SD" — it had been re-received over RF.                               | Medium | Decide: define the flag in the T-Deck envs and bench the persistence, or delete the three functions and the vector. Deleting is the smaller change and frees internal heap. Either way the GUI switch must reflect reality (HL-04).                                                                                                                                                                                                                                                 |
| CQ-04 | `src/t-deck/lv_conf.h` (dead), `variants/t_deck_plus/lv_conf.h` (live)                                                                                 | Two `lv_conf.h`; the one next to the sources is never on the include path (`-D LV_CONF_INCLUDE_SIMPLE` resolves to the variant copy). It carries a different refresh period, font set and `LV_COLOR_16_SWAP`, misled two reviews and one eight-agent research brief (C07, P04).                                                                                                                                                 | Low    | Delete `src/t-deck/lv_conf.h` (flash byte-identical proves it dead) or rename to `lv_conf.h.unused` with a one-line header pointing at the variant file. Add `find . -name lv_conf.h -not -path './.pio/*' -not -path './lib/*'` to the audit.                                                                                                                                                                                                                                      |
| CQ-05 | `loop_functions.cpp` `sendHey()` (`save_settings()` per call), same in `sendPosition()`/`sendPing()`/`SendPong()`                                      | Every originated frame writes the settings blob to flash to persist `node_msgid`. FL-02 capped the HEY rate; the write per send stays (about 170 000 NVS writes in the 2026-08-27 storm). On nRF52 the same write reaches LittleFS from the timer task on the ACK path (§3.8l M5). Pattern A19/C10.                                                                                                                             | Medium | Persist `node_msgid` lazily: bump in RAM, write at most once per N sends or on a 60 s tick from the loop task, and on `--reboot`/shutdown; on boot add a safety offset so ids never repeat after a crash. Native test on the allocator; bench: `grep -c "settings saved"` per hour before/after.                                                                                                                                                                                    |
| CQ-06 | `loop_functions.cpp` (`sendMessage()`, `PositionToAPRS()`), `esp32/esp32_main.cpp`; `platformio.ini` native envs                                       | Neither file is in any native `build_src_filter` (`awk '/^\[env:native/{e=$0} /loop_functions\.cpp/{print e}' platformio.ini` is empty). Consequence today: the `strncat` fix in `PositionToAPRS()` and the map-timer `millis()` fix shipped without a regression test, and 528 green cases never exercise the BP wiring in `sendMessage()` (pattern C28, the BP-07 "integration test" retraction).                             | High   | Extract the `PositionToAPRS()` tag-concat block into `src/pos_concat.{h,cpp}` (pure: inputs are the tag strings, output the bounded concat) and the BP wiring of `sendMessage()` behind a thin seam; list both in a native env; first tests: worst-case tag set fits in 99 bytes and clears `catxt`/`cname` in the documented order (fails-before against the pre-`e108fa98` idiom), and a refused send produces exactly one receipt.                                               |
| CQ-07 | `platformio.ini` `build_src_flags` (ESP32 and nRF52 bases)                                                                                             | `-Wvla`, `-Wformat-truncation=2`, `-Wconversion`, `-Wunreachable-code` are off. `-Wformat-truncation` is what caught `cfwdate[20]`; `-Wvla` would have refused SEC-03's BLE-sized VLAs; `-Wconversion` covers the `uint8_t` length arithmetic of C02 (22 `CQ2-C02b` sites today). Pattern P23 / gap 19 in the GIT report.                                                                                                       | Medium | Measure per flag on all 32 envs (`grep -c warning:` per flag), enable `-Wvla -Wformat-truncation=2` first (expected near zero), then `-Wunreachable-code`; `-Wconversion` only after its count is worked down, never as `-Werror` with a backlog.                                                                                                                                                                                                                                   |
| CQ-08 | `esp32/esp32_flash.h`, `nrf52/WisBlock-API.h` (`s_meshcom_settings`), `configuration_global.h` (`FLASH_STRUCT_VERSION`)                                | No `static_assert` on `sizeof(s_meshcom_settings)`. A sizeof-neutral reordering or a field added in the middle passes every build and the N-12 size guard, and the two platform layouts (2008 vs 1968 B) still differ. Pattern C19 / P21.                                                                                                                                                                                       | Medium | `static_assert(sizeof(s_meshcom_settings) == 2008)` (ESP32) / `== 1968` (nRF52) next to each struct, with a comment that bumping the number requires a `FLASH_STRUCT_VERSION` bump and a migration note. Part of the N-12 struct unification, can land ahead of it.                                                                                                                                                                                                                 |
| CQ-09 | `tools/**/*.py` (about 1000 lines added since 2026-08-28), release skill gate                                                                          | No Python lint or format gate exists; `uvx ruff check tools/bench/tdeck_harness.py` reported 104 findings (101 pre-existing) and the only checks on `rak_harness.py`, `gwflood.py`, `wifisoak.py`, `bootloop.py`, `meshlogger.py` were `py_compile` and their own unit tests. Pattern P09.                                                                                                                                      | Low    | `ruff.toml` with the current findings as an excluded baseline (count may only go down), `uvx ruff check --select E,F,B tools/ && uvx ruff format --check tools/` as a gate line in `/release-firmware` and `/orchestrate-waves`.                                                                                                                                                                                                                                                    |
| CQ-10 | `tools/code_audit_scan.py`, new `tools/pair_diff.sh`                                                                                                   | Three checks from code-quality-2.0.md are not regexes and are not in the scanner: (a) macro tested by `#if`/`defined()` but defined by no env or header (C07; found `HEAP_TEST`, `T_DECK_SPIFFS`, `BOARD_HELTEC_V31`), (b) struct initialiser default vs `preferences.get*()` default per field (C19; found `node_wifion`), (c) the diff of the ESP32/nRF52 function pairs listed under C06 (19 drift instances in the window). | Medium | Add (a) and (b) as functions in the scanner (`--extra` or always-on, JSON findings with rule ids `CQ2-C07x`/`CQ2-C19x`); (c) as `tools/pair_diff.sh` printing a unified diff per pair, run by `/code-audit` when a scanned file is one half of a pair. Each with a fixture test under `tools/mock/`.                                                                                                                                                                                |
| CQ-11 | `.gitignore:21` (`.*`), `.claude/commands/*.md`                                                                                                        | The blanket `.*` rule hides `.claude/commands/`; `release-firmware.md`, `code-audit.md` and `build-firmware.md` are tracked only because they were `git add -f`ed, and any edit to them does not show in `git status`. Two sessions committed skill changes late or not at all for this reason (P07).                                                                                                                           | Low    | Add `!.claude/` and `!.claude/commands/` (or `!.claude/**/*.md`) below the `.*` line; verify with `git check-ignore -v .claude/commands/code-audit.md` (must print nothing). Keep `.claude/settings.local.json` ignored explicitly.                                                                                                                                                                                                                                                 |
| CQ-12 | `src/` (365 files, 130 k lines); last full audit `code-audit-20260712.md`                                                                              | No full audit since 2026-07-12; about 125 fix and wave commits since. The extended scanner reports 778 mechanical hits on `--full` (4 CRITICAL — all `sprintf` in `t-deck-pro/ui_deckpro.cpp` — 59 HIGH, 482 MEDIUM, 233 LOW), most of it pre-existing and never triaged against Part D of code-quality-2.0.md. The T-Deck GUI review alone produced 36 findings in one session, so one pass over everything is not a session.  | High   | `/code-audit --full --delta` in four waves by file family: (1) parsers and protocol (`aprs_`, `mheard_`, `txring_`, `dedup`, `extudp_`, `kiss` if merged), (2) both main loops and the nRF52 paths, (3) web, UDP and EXTUDP, (4) T-Deck and T-Deck Pro. Each wave: Tier 1 + Tier 2 of Part D, HIGH/CRITICAL and every `CQ2-*` hit first, LOW only as a count; result `docs/code-audit-20260902.md` with delta vs 20260712; triage into §3.8u, then fix waves with the advisor gate. |

### 3.8v `--setlog` instrumentation (SL-01..SL-07) (2026-09-02)

Source: [`setlog-instrumentation-impl-plan-20260902.md`](setlog-instrumentation-impl-plan-20260902.md)
(own worktree `feat-setlog-20260902`, based on `v4.35p_prio` @ `3b34dfc1`), triggered by the OE3
mountain-gateway night report (`docs/report-2026-09-02-oe3-bergknoten.htm`) and the adaptive
relay-slot concept report (`docs/report-2026-09-02-adaptiver-relay-slot.htm`). `--setlog on` used
to print exactly one line per reception — no level, no dedup verdict, no relay decision, no own
transmissions — so the three open questions in the night report (signal level, dedup window,
queue latency) could not be answered from a capture. Seven new line kinds close that gap, all
hanging exclusively off `bDisplayLog` (`--setlog on`), none off `--loradebug`, with no behaviour
change. Full field reference: `.claude/skills/logauswertung/SKILL.md` §
"Zeilenreferenz `--setlog on`".

| ID    | Where                                                                                                                                  | Finding                                                                                                                               | Sev.   | Action                                                                                                                       | Status                                                                               |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| SL-01 | `src/lora_functions.cpp` (`OnRxDone`, `handleACK`), `src/loop_functions.cpp` (`printBuffer_aprs()`/`printBuffer_ack()` tail parameter) | RX/ACK line carried no signal level and no dedup verdict; copy rate had to be estimated from raw counts.                              | Medium | Append `RSSI:`/`SNR:`/`DUP:`/`OWN:`/`t=` to the existing, byte-identical line.                                               | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |
| SL-02 | `src/lora_functions.cpp` relay block (`skip_relay:`)                                                                                   | Why a frame was not relayed was invisible in a capture.                                                                               | Medium | New `RLY` line with `q=<code>`, one of ten reason codes in checking order.                                                   | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |
| SL-03 | `src/lora_functions.cpp` (`doTX`/`setlogPrintTx`)                                                                                      | Queue latency, starvation and CAD cost were only visible via straggler timing.                                                        | Medium | New `TX` line with `wait=`/`q=`/`cad=`, printed on successful send start (not on `TX_DONE`).                                 | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |
| SL-04 | `src/lora_functions.cpp` (`OnRxError`), `src/esp32/esp32_main.cpp` (CRC path)                                                          | Collision/error rate was ESP32-only and gated behind `--loradebug`.                                                                   | Medium | New `ERR` line, both platforms, plus an unconditional `stat_rx_err` counter.                                                 | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |
| SL-05 | `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp` (5-minute `PRIO_STAT_INTERVAL_S` tick)                                          | Channel utilisation, ring high-water, drops and heap were only visible under `--loradebug` at 10 s resolution.                        | Medium | New `STAT` line every 5 min: `util=`/`newid=`/`dup=`/`err=`/`txn=`/`txfail=`/`ringmax=`/`drop=`/`mh=`/`heap=`/`trk=`/`fw=`.  | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |
| SL-06 | `src/udp_functions.cpp`, `src/nrf52/nrf_eth.cpp` (server inject), `src/lora_functions.cpp:1372` (`addNodeData` call)                   | Gateway multiplication across several co-visible gateways, and the upload-before-decrement ordering, were not traceable per `msg_id`. | Low    | New `GWI`/`GWU` lines; `ringSource[]` byte marks server-injected ring entries (`src=g` in `TX`).                             | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |
| SL-07 | `tools/berglog.py`, `.claude/skills/logauswertung/SKILL.md`, this file, `CHANGELOG-stability.md`                                       | The new line kinds needed a parser, fixtures and a reference doc, or `berglog.py` would call every one of them `undecodable`.         | Low    | Parser + new report sections (Welle 2, agent E); skill reference + backlog/changelog entries (Welle 2, agent F, this entry). | implemented 2026-09-02 (Welle 0/1/2), bench Welle 3 pending — no bench node attached |

RAM cost across all seven lines: under 64 B (a handful of `std::atomic` counters plus one
`uint8_t[MAX_RING]`), no new buffers — formatters write into the caller's existing `printfdeb`
buffer. Welle 3 (bench proof on RAK4631 DK5EN-90 + Heltec-93, 30-minute capture, cross-check
against `--loradebug`) is open: no bench node was attached on the evening of 2026-09-02
(`ls /dev/cu.*` empty). Upstream PR packaging is prepared but not filed — `src/setlog_lines.*`,
`loop_functions.*`, `txring_functions.*`, `lora_functions.cpp`, both platform mains,
`udp_functions.cpp` and `nrf_eth.cpp` go into the PR; `tools/`, `.claude/`, `docs/` and
`platformio.ini` stay fork-only.

### 3.8w T-Deck message view has no send/delivery indicator (operator report 2026-09-03)

**Intake.** Operator on a T-Deck Plus freshly flashed to 4.35s: _"Habs in vorheriger Firmware bei
den (direkten) Nachrichten immer ein Symbol ob gesendet und/oder die Nachricht empfangen bzw
uebertragen wurde? Hab ich am Deck selber grad ned oder nimmer. Im webinterface jedoch schon."_

**Verdict: not a regression — the T-Deck has never shown one.** Nothing broke in 4.35s; the
feature does not exist on that display and never did. Filed as an enhancement (`TD-11`), not a bug.

**What exists today.** The per-message delivery state is `own_msg_id[MAX_RING][5]`, byte `[4]`
(`src/loop_functions.cpp:402`, reset to `0x00` on insert at `:734`):

| Value  | Meaning                                               | Set at                                                                                          |
| ------ | ----------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `0x00` | not heard — nothing came back                         | `insertOwnTx()`, `src/loop_functions.cpp:734`                                                   |
| `0x01` | heard — another node relayed our own frame back to us | `src/lora_functions.cpp:858`                                                                    |
| `0x02` | ACK — the addressee acknowledged                      | `src/lora_functions.cpp:375`, `:1019`; `src/udp_functions.cpp:417`; `src/nrf52/nrf_eth.cpp:576` |

Consumers of that byte in the whole tree:

- **Web UI** — `src/web_functions/web_functions.cpp:1645-1658`, the only place that reads it.
  It prefixes the message header with `&#x2713;` (`✓`, heard) or `&#x2611;` (`☑`, ACK).
  This is what the operator sees.
- **Phone app** — indirectly, over BLE: an `MSG_TYPE_ACK` frame (`0x41`,
  `src/configuration_global.h:293`) with byte 5 = `0x00` (heard, `lora_functions.cpp:839-847`) or
  `0x02` (ACK, `:1000-1006`). The app renders its own markers from those.
- **T-Deck GUI** — nothing. `tdeck_add_MSG()` (`src/t-deck/lv_obj_functions.cpp:4383`, called
  from `loop_functions.cpp:2568` on RX and `:4083` after own TX) fills a `MsgBubble`
  (`lv_obj_functions.cpp:160`) that holds `type`, `header`, `timestamp`, `body`, `gps`, `sd`,
  `wlan` — no `msg_id`, no status field. The outgoing bubble appears the moment the frame is
  queued and is never touched again.

**History checked.** The pre-tab T-Deck view in upstream `v4.34y` (`381f176a`,
`lv_obj_functions.cpp:1582`) appended a plain `HH:MM path>call\nmessage` string to one textarea —
also no marker. `git log -S` over `src/t-deck` finds no `own_msg_id` reference at any point in the
history.

**Implementation sketch (if picked up).**

1. Carry `msg_id` (and a `uint8_t status`) in `MsgBubble`. Only the `aprsMessage` overload
   (`:4383`) has an id; the `String` overload (`:4434`) has none and currently has no caller, and
   `tdeck_add_system_message()` (`:4250`) never needs one — so status stays "unknown" there.
2. On ACK/heard, upgrade the bubble and repaint one label. **Do not cache the `lv_obj_t *`** —
   `tdeck_reset_msg_tabs()` (`:4472`) destroys and rebuilds every bubble on a tab switch, and stale
   LVGL pointers are exactly the `G01`/`H1` class of defect from §3.8c/§3.8e. Look the bubble up by
   `msg_id` and re-render from the model instead.
3. The four setter sites listed in the table above are the hooks; `nrf_eth.cpp` is irrelevant for
   the T-Deck (ESP32-only board), so three of them matter.
4. **The awkward part is persistence.** `save_persisted_messages()`
   (`src/t-deck/lv_obj_functions.cpp:3426`) writes JSON lines with exactly
   `group`/`type`/`timestamp`/`header`/`body` to SPIFFS and rewrites the **whole** file on every
   flush. Two consequences: the format needs two new optional fields with a loader that tolerates
   their absence in existing files (`load_persisted_messages()`, `:3524`), and a status upgrade must
   ride the existing `unsaved_msgs_count` / `last_flush_millis` debounce rather than triggering a
   full file rewrite per ACK.
5. Cosmetic caveat: `own_msg_id` is a ring of `MAX_RING` entries, so the status of an old message
   ages out of the model while its persisted bubble survives a reboot. Decide whether a restored
   bubble shows its last known status or no marker at all.

**Effort:** small, ~1 file plus the persistence format. No RF or protocol change, ESP32-only, no
upstream coupling — a fork-local GUI item.

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

### 4.1 Branch model (decided 2026-08-29, branch renamed 2026-09-03)

```
upstream/dev --merge--> fork-main --branch--> topic branch (tdeck-...)
      ^                        |                                  |
      |                        |<------------- merge back --------+
      +---- PR <-- pr/<topic> <+  (built from upstream/dev + firmware files only, 1 commit)
```

- **`fork-main` is fork main and the permanent home of everything**: docs, `tools/`, `test/`,
  `src/instrument.*`, `src/test_inject.*`, `src/t-deck/tdeck_debug.*`, the bench harness. It
  tracks upstream by **merge, never rebase** (icssw-org squash-merges; a rebase turns our merged
  fixes into deletion-only commits — memory `merge-not-rebase-after-upstream-squash`).
- **Topic branches** off fork main for active work; merged back when done, then deleted.
- **PR branches are built, not branched**: `git checkout -b pr/<topic> upstream/dev`, then
  `git checkout fork-main -- <firmware files>`, cut the four couplings (memory
  `firmware-only-pr-coupling`), squash to one commit with the German per-file description. No
  docs, tools, tests or debug code in a PR. After upstream squash-merges, `git merge upstream/dev`
  into fork main sees identical content and the surrounding debug hooks survive.
- **Debug code rule:** new instrumentation goes into the dedicated files above with one-line
  hooks in production code — that keeps the conflict surface with upstream refactors minimal.
  For v5 the durable path is to offer the instrumentation upstream as a default-off compile
  option in its own PR once the T-Deck PR has landed.

### 4.2 Branches as of 2026-09-03

| Branch               | State                                                          | Decision                                              |
| -------------------- | -------------------------------------------------------------- | ----------------------------------------------------- |
| `fork-main`          | fork main, 0 behind `upstream/dev` `4e649eae` (firmware 4.35s) | keep — carries docs, `test/`, `tools/`, bench harness |
| `v4.35p_prio`        | previous name of `fork-main`, last 4.35p state `1d0bd23a`      | tag `archive/v4.35p_prio-20260903`, branch deleted    |
| `pr-1114-kiss`       | KISS/TCP v1.3, 8 commits, worktree `mc-pr1114-kiss`            | keep — not upstream yet                               |
| `master`, `gh-pages` | fork mirror / pages                                            | ignore                                                |

Upstream cut `4.35s` on 2026-09-03 (`c908a4dd`: `SOURCE_VERSION_SUB` `p` -> `s`, plus the removal
of the `else node_postime = 0` branch in `--postime` that had reset every value >= 300 s to the
default). The fork branch was named after the upstream version letter and therefore went stale on
every such cut, so it was renamed to the version-neutral `fork-main` in the same pass; the fork's
default branch on GitHub moved with it. Historic reports keep the old name on purpose — they
record the tree as it was.

Consequence of the upstream `--postime` change: `--postime 0` no longer disables position beacons
(0 < 300 clamps to 300). Inherited as-is; if "positions off" should stay reachable it needs its
own upstream PR.

### 4.2a Branches as of 2026-09-01 (historical)

| Branch                        | State                                               | Decision                                                    |
| ----------------------------- | --------------------------------------------------- | ----------------------------------------------------------- |
| `v4.35p_prio`                 | fork main, synced with `upstream/dev` `898ff7e5`    | keep — carries docs, `test/`, `tools/`, bench harness       |
| `tdeck-partial-refresh-trace` | content fast-forwarded into `v4.35p_prio`           | tag `archive/tdeck-partial-refresh-trace-20260901`, deleted |
| `pr-firmware-20260901`        | merged upstream (#1115), ahead 0                    | deleted local + origin                                      |
| `pr-upload-port-20260901`     | merged upstream (#1118), ahead 0                    | deleted local + origin                                      |
| `pr-20260901`                 | superseded full-fork squash, content in fork main   | deleted local + origin                                      |
| `pr-1114-kiss`                | KISS/TCP v1.3, 8 commits, worktree `mc-pr1114-kiss` | keep — not upstream yet                                     |
| `master`, `gh-pages`          | fork mirror / pages                                 | ignore                                                      |

Between 2026-08-29 and 2026-09-01 `tdeck-partial-refresh-trace` had become the de-facto fork
main — 154 commits ahead of `v4.35p_prio`, which held nothing it lacked. The sync restored the
model of §4.1 by fast-forwarding `v4.35p_prio` onto it, so the branch named fork main is fork
main again.

### 4.2b Branches as of 2026-08-29 (historical)

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
