# Campaign UPNEXT-01: the next upstream PRs after #1346

**Planned 2026-11-28, not started. It brings the rest of our open upstream issues to PRs: #1333, the library patches #1330, #1331, #1332, #1335, #1336, and #1309, #1340, later #1311. The first step is the sync after #1346 is merged; nothing starts before that.**

## Resume here

- **State:** plan only. No wave has run. PR [#1346](https://github.com/icssw-org/MeshCom-Firmware/pull/1346) (seven small fixes) is open; worktree `../MeshCom-PR-next`, branch `pr/hardening-small-fixes`.
- **Next step:** wave 0 as soon as #1346 is merged (or closed). Check: `gh pr view -R icssw-org/MeshCom-Firmware 1346 --json state,mergeCommit`.
- **Waiting on the operator:** the four decisions in wave 2. They can be taken any time before wave 3.
- **Mechanics:** `/orchestrate-waves` (file ownership, one `pio` at a time, advisor before each behavioural commit), `/sync-upstream`, `/submit-pr`, `/bench`.

## Scope

| Issue | What                                                            | Fork code                                                                                                           | Missing before a PR                                                                                     |
| ----- | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| #1333 | DNS lookups in the lwIP tcpip thread                            | done (`9696a3ba`: `src/esp32/mc_dns.*`, AU resolver in `McTlsClient.cpp`, APRS-IS, gateway)                         | EXTUDP caller depends on the fork's async resolver (`04f475a4`); decision D2; bench                     |
| #1330 | NimBLE-Arduino 2.2.3 patches                                    | done (`6bbca7a2`, `9696a3ba`, `tools/nimble_patch.py`, 300 B attributes F3 `71705b36`)                              | 8 envs lack the prepare-write limits (NIMBLE-PREP-VARIANTS-01), decision D1; bench with the phone       |
| #1331 | arduino-esp32 2.0.17 patches AE-01..03                          | done (`tools/arduino_esp32_patch.py`, AE-01 v2 `03a30cac`), benched 2026-11-24/25                                   | port only                                                                                               |
| #1332 | mDNS and DHCP server backports, DHCP MTU floor                  | done (`src/esp32/idf_override/`, about 8,000 vendored lines; `lwip_mtu_guard.*`, `tools/lwip_wrap.py`)              | bench of softAP DHCP and mDNS (never run)                                                               |
| #1335 | RAK13800-W5100S socket index guards (ETH-07)                    | done (`tools/w5100s_patch.py`)                                                                                      | bench on DK5EN-90                                                                                       |
| #1336 | Safeboot on ESP32Async/ESPAsyncWebServer 3.12.1, AsyncTCP 3.6.1 | done (4 lines in `platformio.ini`)                                                                                  | bench: Safeboot upload on classic and S3, abort, second upload; rebuilt `safeboot*.bin`                 |
| #1309 | Safeboot rest: SB-06, gzip upload page, drop staged image       | done (`d1ec388e`, `87e3bcd9`; SB-06 gate entangled with `afaa2f40` in `src/safeboot/ElegantOTA.cpp`, `app_check.h`) | untangle for the port; bench of gzip page and staged drop                                               |
| #1340 | `{CET}` restamp before LoRa TX (Option B, decided 2026-11-26)   | none; concept `docs/concept-cet-restamp-01-20261116.md`                                                             | implementation, server dedup check of a restamped copy (decision D4), field time                        |
| #1311 | Relayed ACKs wait 6-25 s (ACK-AGE-01, CSMA-01/03/05, IDEE-04)   | code in the fork, parameters not final                                                                              | load run, nRF52 bench, 24 h soak; operator gate: PR not before 2027-01-29. Outside this campaign's PRs. |

## Waves

| Wave | Content                                                                                                                                                                                                                                         | Who                                                                              | Wall-clock, cost (estimate)                                        |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| 0    | Sync after #1346: `/sync-upstream` (merge path, our paths keep the fork side, tree unchanged afterwards), full gate `--stage 1,2`, remove worktree and branch `pr/hardening-small-fixes` local and on origin                                    | orchestrator alone                                                               | 30-45 min, about $5                                                |
| 1    | Recon on the new `upstream/dev`, read-only, parallel: (a) port map library patches, (b) port map #1309 and the `afaa2f40` untangle, (c) port map #1333 with and without the async resolver, (d) #1340 hook points and the server dedup question | 4 `scout`, Haiku, medium                                                         | 15 min, under $2                                                   |
| 2    | Operator decisions D1-D4 (below)                                                                                                                                                                                                                | operator                                                                         | -                                                                  |
| 3    | Fork code: (A) #1340 restamp helper, TX hook, host tests, red before; (B) NIMBLE-PREP-VARIANTS-01 flags per D1. Gate `--changed`, advisor on A                                                                                                  | A: 1 `implementer` Sonnet medium; B: orchestrator (a few ini lines)              | 40 min, about $10                                                  |
| 4    | FORMAL01-BENCH, one hardware wave, serialized (see bench list)                                                                                                                                                                                  | orchestrator; operator for the phone, buttons and Safeboot upload in the browser | 2-3 h, about $15                                                   |
| 5    | PR branches, one worktree each, pinned to the `upstream/dev` SHA: port by implementers in parallel (no `pio` inside), builds, overlay host tests and PR-image bench serialized at the gate; advisor per PR diff; `/submit-pr` Steps 6-8         | 2 `implementer` Sonnet medium (one per PR), orchestrator, Opus advisor           | per PR 1.5-2 h (33-env build alone about 27 min), about $15 per PR |
| 6    | #1340 PR after field time; #1311 after its soak (from 2027-01-29)                                                                                                                                                                               | later                                                                            | -                                                                  |

## Proposed PR cut

- **PR A, library patches:** #1331, #1335, #1336, #1330, #1332. Build pre-scripts under `tools/` plus their `extra_scripts` lines, the vendored `idf_override/` sources, `lwip_mtu_guard`. Large but mechanical; each patch is version-gated and a no-op without its library.
- **PR B, Safeboot and DNS:** #1309 rest and #1333 (scope per D2). Needs rebuilt `safeboot.bin` and `safeboot-s3.bin`.
- **PR C, `{CET}` restamp:** #1340, after field time on the fork.
- #1311 stays separate (2027).

## File ownership

| Wave, task      | Exclusive files                                                                                                                                                               | Notes                                                                                         |
| --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| 0 sync          | whole tree                                                                                                                                                                    | orchestrator only, no agents                                                                  |
| 1 scouts        | none (read-only)                                                                                                                                                              | unlimited parallel                                                                            |
| 3A #1340        | new `src/cet_restamp.h`, new `test/test_cet_restamp/`, the TX hook hunk in `src/lora_functions.cpp`                                                                           | `platformio.ini` env block, suite inventory and golden ini baseline: orchestrator at the gate |
| 3B NimBLE flags | `variants/t_deck_pro/`, `t5_epaper/`, `vision-master-e213/`, `vision-master-e290/`, `wireless-paper/` `platformio.ini`; `[env:esp32-external-radio]` in root `platformio.ini` | orchestrator itself (hotspot file)                                                            |
| 5 PR A port     | worktree `../MeshCom-PR-libs` only                                                                                                                                            | no `pio`; builds at the gate                                                                  |
| 5 PR B port     | worktree `../MeshCom-PR-safeboot` only                                                                                                                                        | no `pio`; Safeboot binaries rebuilt by the orchestrator                                       |

Shared resources: one `pio` process at a time (global package store, `test/golden/selftest.sh`); every bench node and serial port is a gate step; DK5EN-98 is not used.

## Wave 4 bench list (FORMAL01-BENCH)

- **NimBLE (#1330):** phone long write of 300 B and BLE pause/resume around an Auto Update check on Heltec V3 and T-Deck Plus (MTU at least 155). Operator's phone.
- **Safeboot (#1336, #1309):** multipart upload on T-Beam (classic) and Heltec V3 (S3), one aborted upload, then a second one; gzip page loads; a non-app file is refused (SB-06); a staged image is dropped on web upload.
- **softAP DHCP and mDNS (#1332):** a node in AP mode hands out a lease to the Mac and a phone; `dk5en-NN.local` resolves; lease renewal after 10 min.
- **W5100S (#1335):** DK5EN-90 Ethernet gateway, cable pull and replug, 30 min run, server connected.
- **DNS (#1333):** AU check, APRS-IS login and EXTUDP to a DNS name on Heltec V3 and T-Beam; no task watchdog during a slow resolver.
- **`{CET}` restamp (#1340):** `tools/bench/clock_drift.py` on two nodes, one with NTP; age of the beacon at the receiver before and after.

## Decisions for the operator (wave 2)

- **D1 NimBLE on the eight extra envs:** give `t_deck_pro`, `t5_epaper`, `vision-master-e213`/`-preview`, `vision-master-e290`/`-otp`, `wireless-paper` and `esp32-external-radio` the two prepare-write limits (recommended), or list them as not covered in PR A.
- **D2 #1333 scope:** port the fork's async EXTUDP resolver with it, which closes the issue but adds about 110 lines (`04f475a4`) and its own bench step; or leave EXTUDP out and only reference #1333.
- **D3 PR cut:** PR A and PR B as above (recommended), or one combined PR.
- **D4 #1340:** confirm Option B is unchanged and who checks the server side (does the server dedup a restamped copy).

## Acceptance per wave

- **Wave 0:** `git diff upstream/dev fork-dev -- <the 19 PR paths>` shows only fork-only lines; gate `--stage 1,2` green; worktree and branch gone.
- **Wave 3:** every new test red on the old code, green after; gate `--changed` green; advisor APPROVED for 3A.
- **Wave 4:** every bench item has a dated result or is listed as not run, with the reason.
- **Wave 5:** per PR: all 33 default envs build, overlay host tests with every out-of-scope failure listed, PR-image bench, advisor APPROVED, operator `yes` before `gh pr create`.
