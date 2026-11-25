# RESUME -- pick up here

**Resume here: section "Now" below. Next: the combined upstream PR (wave 4): decision brief, then the PR branch built and benched, ready to push and file on 2026-11-26.** Release plan and working checklist: operator's
Desktop `upstream-releaseplan-20261125.html` (Fassung 12). Active campaigns:
[campaign-ota-lowheap-01-20261122.md](campaign-ota-lowheap-01-20261122.md),
[campaign-release-train-20261115.md](campaign-release-train-20261115.md) (waves).

## Now (2026-11-25)

- **WEBHOST-1323 on `fork-dev` 2026-11-25 (`430b6254`, `f4d49105`):** upstream issue #1323, a public DynDNS name was
  refused by the web guard (403). New `--webhost <name>|none`, setup field "Public web host", setting `node_webhost`:
  one name, exact case-insensitive match, any port, no wildcards; empty keeps the old guard. Gate stage 1,2 PASS
  (4,241 Unity cases); Opus advisor APPROVED; bench DK5EN-14 PASS
  (`tools/bench/runs/harness/webhost-1323-20261125.summary`), name cleared afterwards. Next: `/submit-pr`.
  Found on the way: in a fresh clone `test_arduino_esp32_patch` fails (`text=auto` turns the CRLF in
  `tools/tests/fixtures/au_http/*.hdr` into LF) and the two heap-timeline test logs are gitignored, so missing.
- **Released `v4.40a.11.25` (2026-11-25 20:16, Latest, 69 assets) on `fork-dev` `9586d82d`:** Auto Update without the
  low-memory refusal (OTA-LOWHEAP-01, fork issue #3 closed by the release commit), AU-29, AU-30, nRF52 sync word
  `0x24B4` (#1320), wave-3 fixes (#1316), upstream #1321 (synced `cd262938`), TD-35, TD-36. Gate stage 1,2 PASS (4,237
  Unity cases, 1,842 pytest); T-Beam idle heap 64,440 B (+424 B); all 28 `.autoupdate` re-inflate byte-exact; web
  flasher on 11.25 only (31 boards, 0 mismatches), Pages synced. Bench DK5EN-1, -14, -90, -92 on local builds of the
  release. DK5EN-98: the operator runs its Auto Update.
  - Two false starts, both without a published release: the first CI run failed because `release-notes.md` exceeded
    GitHub's 125,000-character release body (fork releases 10.11 to 10.30 moved to
    `docs/archive/release-notes-fork-202610.md`); the second was cancelled for the #1321 sync. The tag was moved twice
    with the operator's consent. Follow-up: a size check for `release-notes.md` in the gate (RELNOTES-SIZE-01).
  - `test_ext_reg` key audit caught the new BLE key `MWAKE` from #1321 (reviewed public, `23347203`).
- **T-Deck fixes 2026-11-25 (from a T-Deck user report on 11.22):** TD-35, the heap trim no longer deletes tabs (the
  public tab vanished at runtime); TD-36, no grey light-theme shadow on the toggle and segment buttons. Committed on
  `fork-dev` (stage 1,2: 4,237 Unity cases green, suite inventory regenerated, change gate PASS); TD-36 bench screenshot PASS on DK5EN-14; TD-37 (re-tune the 40 KB heap floor) open in BACKLOG "Implement".
- **Campaign RC-BENCH closed 2026-11-25 07:30:** [campaign-rc-bench-20261125.md](campaign-rc-bench-20261125.md),
  section "Close". Release candidate `v4.40a.11.25` ready, not tagged. Night 2026-11-24/25: Auto Update check and
  download 3x on DK5EN-1 (V3), -92 (T-Beam), -14 (T-Deck Plus), 9/9 staged, no reboot, no refusal; T-Beam deferred the
  image check to the verify boot twice (AU-30 path). DK5EN-98 on the RC image 15.8 h without reboot. Host gate PASS
  (4,235 Unity cases in 129 envs, pytest 1,842) after a test-isolation fix in `tools/tests/test_hmac_connect.py`.
  - **Waiting for the operator:** go for tag `v4.40a.11.25`; go for the combined PR plus its scope questions (#1306
    QA-RX-07/02, #1307 `--udphold` wording, #1308 EXT-PEER-01, #1309 "Refs" only); PR-branch build of every env and a
    short bench run (no FORMAL-01 in the branch); go for the three issue drafts.
  - Documents: `release-notes.md`, `docs/release-journal.md`, PR text
    [pr-combined-text-20261125.md](pr-combined-text-20261125.md), scope
    [pr-combined-scope-20261125.md](pr-combined-scope-20261125.md), issues
    [upstream-issue-drafts-20261125.md](upstream-issue-drafts-20261125.md).
  - Operator decisions 2026-11-24: no mbedTLS copy, one combined upstream PR (TLS allocation cap travels with AU),
    desk commit `35ae3418` (WX table) ships host-tested, sensitivity margin not testable on the table.
- **Upstream sync 2026-11-24:** merged `upstream/dev` `68286e07` (#1320): nRF52 sync word `0x242B` -> `0x24B4` from one
  source (`src/lora_syncword.h`, `154014dc`) and two `tools/hmac_connect.py` commits. No conflicts; macro baseline
  regenerated; host gate stage 1,2 PASS (five board builds, not every env). Code review (Opus, high): sound as code.
  The mixed old/new nRF52 bench test and the build of every env are done (campaign RC-BENCH). `HMAC-WIN-01` (Windows hang in `hmac_connect.py`) is upstream's to fix.
- **Next upstream PR (plan Fassung 12):** one combined PR, wave 4: Auto Update (OTA-LOWHEAP-01, AU-29, AU-30,
  AU-23, TLS allocation cap), small fixes (#1305, #1306, INA226), gateway (#1307, #1308), remote management (#1303),
  `hmac_connect.py`. Still open: the NIGHTLY-01 condition (operator).
- **TESTGATE-01 closed 2026-11-24:** see "Running campaigns". The FORMAL-01 patches and the formal rework noted below
  as uncommitted are committed (`9696a3ba`, `6bbca7a2`, `8cc67569`); the "likely GitHub-side" download failure below
  was AU-28, the fork's own HTTP client patch, fixed.

## Earlier (2026-11-23)

- **Formal checks reworked, unrun (2026-11-23):** review [formal-review-20261123.md](formal-review-20261123.md), how to
  run [`specs/README.md`](../specs/README.md). Runners repaired, tautologies removed, `netconsole.tla` rewritten, heap
  models archived, tiers and budgets for the bounded jobs, self-test and drift check in stage 2 of the host gate. Runs stay
  local on the Mac (operator: no GitHub run). Models were edited without a parser and nothing ran: BACKLOG
  `FORMAL01-RUN` is the next step and starts with `specs/tla/run_tlc.sh --sany-all`. Predicted, unconfirmed defect in
  the net console slot takeover: BACKLOG `FORMAL-NETCON-MARK`. Not committed.

- **FORMAL-01 (library patches and formal-check fixes), code phase:** campaign
  [campaign-formal-01-20261123.md](campaign-formal-01-20261123.md), status per review finding
  [review/formal01-20261123/README.md](review/formal01-20261123/README.md). Operator 2026-11-23: finish the fixes now
  (code, review, fix, commit); tests and bench later (BACKLOG `FORMAL01-BENCH`, `FORMAL01-GAPS`); TLA+/BMC another day
  and only on explicit request (`FORMAL01-RUN`, `tools/regression.sh --formal`). Open next: `FORMAL01-1B`,
  `FORMAL01-SEAMS`, `FORMAL01-UP`.
  - In the tree, uncommitted: 3a/3b library patches (NimBLE NB-01..07, W5100S ETH-07, safeboot ESP32Async bump, lwIP
    MTU guard, arduino-esp32 AE-01..03, TLS date check + TLS 1.2), 3c (patched mDNS and DHCP server objects, NimBLE
    max length 300 + prep entries 2, TLS allocation cap, DNS via tcpip thread), 3d (UDP drain F2-5, queueExtern
    F2-14, net console slot F2-10, APRS F3-8, NUL in asset URL), the `--update dlverify` switch.
  - Next: advisor verdict on 3b+3c, then the gate (board builds sequential, link proof for the overrides, host stage
    1,2 without `--formal`), then commit on fork-dev.

- **AU-28 / AU-29 / AU-30 fixed (2026-11-24):** download `conn -7` (AE-01v2), T-Beam TASK_WDT in the staging erase,
  and the T-Beam heap at the web Install: early stack restart, dry run deferred to a verify boot before WiFi when memory
  is short, one BLE pause per click. Bench: T-Beam 10/10 web installs, Install now -> verify boot -> Safeboot -> 11.22
  PASS; V3 unchanged. DK5EN-92 now runs CI 11.22, DK5EN-1 the AU-30 bench build with 11.22 staged. Campaign doc
  section "2026-11-24".

- **OTA-LOWHEAP-01 running (fork issue DK5EN/MeshCom-Firmware#3):** ESP32-S3 Auto Update refuses with "Not enough
  free memory" after long uptime (field, 11.22 rollout). Campaign
  [campaign-ota-lowheap-01-20261122.md](campaign-ota-lowheap-01-20261122.md); analysis
  [rca-ota-lowheap-01-20261122.md](rca-ota-lowheap-01-20261122.md); RAM hunt
  [ota-lowheap-ram-hunt-20261122.md](ota-lowheap-ram-hunt-20261122.md). Subsumes AU-24 and AU-25.
  - Done 2026-11-22: CI static RAM gate on the ESP32 linker regions (`06b3a6db`); 21 KB growth on the V3 attributed
    (71 % upstream); operator decisions D1-D10 (measure first, no TLS reserve, CI fails above +1 KB, mailbox on
    demand, hold 1 KB, rings 2 KB, NBR unchanged). Code fixes wait for the red.
  - Overnight run 2026-11-22 14:30 to 2026-11-23 07:30: INSTRUMENT `1184177d` on DK5EN-1 (gateway, 2 dBm, operator
    exception), -92, -14, -90 and DK5EN-98 (OTA-flashed 15:02, BLE McApp, reference only), displays off on the OLED
    nodes, `--heaplog 60`. Detached processes on the Mac (PPID 1, not screen): meshloggers, `serial_capture.py`
    (RAK), `reboot_watch.py`, `caffeinate`. Logs: `tools/bench/runs/harness/ota-lowheap-night-20261122/`.
  - Evening 2026-11-22: web GUI of all 5 nodes open in Chrome, an in-page cycler loads one page per minute (web load
    like the field). Heap flat, no leak; the first AU check after boot splits the largest block once (86 -> 61 KB on
    the V3), repeated checks do not (ratchet test). Advisor: the split is not the field cause; next tests are the
    splitter owner (`--heapwatch`/`--heapgap` INSTRUMENT build, image saved as `heapwatch-v3-firmware.bin/.elf` in the
    run folder) and warm reboot vs power cycle with phone + update page on 11.21. Plan in the campaign doc.
  - 2026-11-23 06:00: `tools/bench/ota_lowheap_trigger.py` runs check + install (stage only, never apply) three
    times on DK5EN-1, -92, -14. Next: read `trigger.summary`, the `[INSTR-HEAP]` curves (free, largest block) and
    `reboot-events.log`; red = `lowheap` refusal. Then the fix wave (update after a fresh boot, D7-D10) with
    red-before tests; DK5EN-1 stays gateway and DK5EN-98 stays on the INSTRUMENT image until the new fork release on 2026-11-23, which upgrades the whole fleet (operator).
  - 2026-11-23: fixes A + B committed (`878ebef4`), V3 green on the INSTRUMENT image. A/B test: chain verification
    off on the download host saves nothing measurable (67.0 vs 66.6 KB), switch removed.
  - 2026-11-23 evening: knockout runs R0-R7 done on DK5EN-1 and DK5EN-92 (`52f044b9`,
    `tools/bench/ota_lowheap_knockout.py`): the download needs 66-69 KiB whatever is on; web server off is the only
    switch (+6 KiB). Half the peak are the two 16717 B mbedTLS record buffers. T-Beam with web off passes the gate and
    downloads, then fails `nomem` on the 32 KB inflate dictionary (largest block 22.5 KB). T-Beam PSRAM: built with
    `BOARD_HAS_PSRAM`, reports 0 at runtime (open).
  - 2026-11-23 evening: mbedTLS output buffer 4 KB + (a') arena committed (`469bb942`, advisor ok, gate PASS, link
    proof esp32/esp32s3). Bench: check job runs with the arena on V3 and T-Beam. T-Beam gets the 32 KB buffer in both jobs on the plain image and passes the gate with web on (the earlier "no buffer" came from the heapwatch image; plain images only from now on). **Open:** the download fails on every image incl. pre-FORMAL-01 builds since about 18:10 (`tls 51/52` is the socket fd, a logging bug; Mac download works; likely GitHub-side, not proven), so the image check after the download is unmeasured. With the reserved buffer the 4 KB send buffer saves no job peak (accepted). T-Beam PSRAM: unflagged on
    purpose (R3-02/MEM-04), closed. Resume: campaign doc, last section. Desktop:
    `ota-lowheap-checkliste-20261123.html`, `-heapmap-`.
- **PR #1316 merged upstream (2026-11-22), synced:** [campaign-pr-wave3-20261122.md](campaign-pr-wave3-20261122.md).
  21 issues closed (#1287-#1302, #1304, #1310, #1312, #1314, #1315). Merge `dc5ea81c` keeps fork-dev on every path
  (upstream copies differ only in comments); tree unchanged, gate stage 1,2 PASS, V3 and RAK4631 build. Worktree
  `../MeshCom-PR-next` and `pr/wave3` (local and origin) removed. Open: 13 desk links (`1830d8ed`, `45732840`) wait for the desk's
  next fork-dev sync. Tag note: upstream now has its own `v4.40a.11.22` (on `5db0a4f5`), name clash with the fork
  tag of the same name; `v4.40a.11.23` is free on both remotes.
- **Open upstream tracking issues, all implemented on fork-dev (checked 2026-11-22):** #1303 (RM cards, RM1
  `pins`/`gpio`), #1305 (RAM-WSP-01), #1306 (QA-RX-03, BL-02; PR ~12-04), #1307 (`--udphold`), #1308 (EXTUDP register
  query; ~12-11), #1309 (AU schedule, AU-23, SB-06; ~12-18), #1311 (ACK-AGE-01, CSMA-01/03/05, IDEE-04; after the 24 h soak). Bench still owed: SB-06, RM Queues
  card Read button, ACK-AGE-01 beyond DK5EN-98.
- **New finding (2026-11-22, upstream behaviour):** with `--gps on` and no GPS module the ESP32 main loop resets
  `posinfo_interval` to 1800 s after 10 GPS polls, so `--postime` has no effect (`esp32_main.cpp`, `igps`). Not
  filed yet.
- **Wave 3 fixes done (2026-11-22):** [campaign-wave3-fixes-20261122.md](campaign-wave3-fixes-20261122.md), all 11
  issues gated and benched, upstream in #1316; #1314 (T114 dark theme, `--msgwake`) `57acc7c3`
  ([campaign-t114-dark-20261122.md](campaign-t114-dark-20261122.md)), T114 colours still field.
- **Wave 3 by 2026-11-26 (operator 2026-11-21):** 2-4 topical PRs from `upstream/dev` (network/gateway,
  commands/APRS, display/position, update/sensors). Content: fork fixes already done (ETH-06, `temp(aprswx)` as a
  real fix, CMD-DASH-01, AU-27, TD-LOCK-01, TBEAM-SLEEP-01) plus the ticket fixes that pass gate and bench by
  2026-11-24; the rest goes with wave 4 or a second fix PR. Order: SEC-DUMP-01 first, then plan by plan.
- **Upstream issues** (icssw-org, English, assigned to DK5EN; PR bodies use `Closes #N`): not filed are AppLink
  (upstream #1276 exists; never post there). Closed topics: see
  [campaign-pr-wave3-20261122.md](campaign-pr-wave3-20261122.md).
- **Operator open:** accept the desk close proposals P-0053, P-0104, P-0125; reopen P-0167 at the next problem-file
  sync; CET-RESTAMP-01 answers; NIGHTLY-01 parked; remote branch `origin/backup/fork-dev-code-review-xdxqzv-fbb5e966`
  (19 commits not in fork-dev, 2026-10-26) kept until the operator decides.
- **Field open:** ETH-06 confirmation on DB0AIS-12 (operator answers 2026-11-22: EXTUDP to MeshDash is off, so the hang path is idle there; router logged nothing; details in `issue-rak-dhcp-after-reboot-db0ais.md`); AppLink app side with the app maintainer; app side of unsigned
  pong ids.

## Archive

- Day detail 2026-11-15 .. 2026-11-21: [archive/resume-journal-20261115-20261121.md](archive/resume-journal-20261115-20261121.md). Release `v4.40a.11.22` facts: [release-journal.md](release-journal.md).

## Fleet

- **DK5EN-98** (production gateway) and **DK5EN-14** (T-Deck Plus, 192.168.68.77): CI image `v4.40a.11.22` via OTA.
  DK5EN-98: `--setlog on` until the ACK-AGE-01 run under load, then off (OK given 2026-11-15); any other work needs
  an explicit OK.
- **DK5EN-1** (new Heltec V3, .80), **DK5EN-90** (RAK, INSTRUMENT, .78), **DK5EN-92** (T-Beam): local images of the
  11.22 commit; AppLink off, ExtUDP off on -1 and -90.

## Running campaigns

- **Test gate rework (TESTGATE-01):** closed 2026-11-24, [campaign-test-gate-20261123.md](campaign-test-gate-20261123.md). Full
  host gate green (162 s warm); `tools/regression.sh --changed` is the gate for wave commits. Step 3.8 (extraction from `src/`) deferred.
- **Ticket triage 2026-11-21:** [campaign-ticket-triage-20261121.md](campaign-ticket-triage-20261121.md) -- the fixes for wave 3.
- **Release train 2026-11-15:** [campaign-release-train-20261115.md](campaign-release-train-20261115.md) -- the active one.
- **Channel access stabilisation** (ACK-AGE-01, ACK-CANCEL-01, CSMA-04/02, CSMA-03, PWR-01/02, soak): dates and exit
  criteria in the campaign doc, section "Parallel tracks".
- **Upstream waves 2-7:** the release plan is the operator's Desktop HTML (Fassung 12, 2026-11-25, with the checklist), not a repo file; the
  repo copy was removed 2026-11-16 (operator: double work, always outdated). Wave list also in the campaign doc.
- **GPSV4-01, GPIO-01, INA226-HELTEC-01:** field feedback only (BACKLOG "Field").

## Rules and recipes

- Branch model, gates, toolchain traps, bench rules: [CLAUDE.md](../CLAUDE.md). Bench fleet, ports, flashing: the
  local `/bench` skill and `tools/bench/fleet.json`.
- Every local fork build: `export MC_AU_DEV_CHANNEL=1` (dev update channel is a build flag since `6c84a46c`).
- Releases: `/release-firmware`; never a same-day `.N` release (the AU tag parser rejects it). Read `OTA-LOWHEAP-01` (Auto
  Update low heap) in the backlog before the next release.

## Where things are

- Open work: [BACKLOG.md](BACKLOG.md) 3.2. Closed rows since 2026-11-16:
  [archive/backlog-closed-20261116.md](archive/backlog-closed-20261116.md), since 2026-11-15
  [archive/backlog-closed-20261115.md](archive/backlog-closed-20261115.md); full row history:
  [archive/backlog-rows-20261107.md](archive/backlog-rows-20261107.md), older
  [archive/backlog-closed-20261024.md](archive/backlog-closed-20261024.md).
- Fork vs upstream feature list: [features-vs-upstream-20261114.md](features-vs-upstream-20261114.md).
- Defect IDs: [architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md). Tests:
  [test-suite-map.md](test-suite-map.md).
- Releases: [release-journal.md](release-journal.md), [release-notes.md](../release-notes.md). Plan view:
  [campaign-gantt.html](campaign-gantt.html).
- Older session journals: `archive/resume-journal-*.md`.
