# RESUME -- pick up here

**Resume here: section "Now" below. Next date: upstream wave 3, two to four fix PRs by Thu 2026-11-26.** Active
campaigns: [campaign-ticket-triage-20261121.md](campaign-ticket-triage-20261121.md) (the fixes),
[campaign-release-train-20261115.md](campaign-release-train-20261115.md) (waves). Release plan and working checklist: operator's Desktop
`upstream-releaseplan-20261122.html` (Fassung 10; the separate Desktop checklist is retired).

## Now (2026-11-23)

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
    proof esp32/esp32s3). Bench: check job runs with the arena on V3 and T-Beam. **Two stops for the operator:**
    (1) T-Beam install job gets no arena (largest block 27.6 KB after the check; recommendation: hand the check
    job's arena to the install job, else fallback (b)); (2) the download fails on every image incl. pre-FORMAL-01
    builds since about 18:10 (`tls 51/52` is the socket fd, a logging bug; Mac download works; likely GitHub-side,
    not proven). With an arena the 4 KB buffer saves no job peak (advisor, accepted). T-Beam PSRAM: unflagged on
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

- **Ticket triage 2026-11-21:** [campaign-ticket-triage-20261121.md](campaign-ticket-triage-20261121.md) -- the fixes for wave 3.
- **Release train 2026-11-15:** [campaign-release-train-20261115.md](campaign-release-train-20261115.md) -- the active one.
- **Channel access stabilisation** (ACK-AGE-01, ACK-CANCEL-01, CSMA-04/02, CSMA-03, PWR-01/02, soak): dates and exit
  criteria in the campaign doc, section "Parallel tracks".
- **Upstream waves 2-7:** the release plan is the operator's Desktop HTML (Fassung 10, 2026-11-22, with the checklist), not a repo file; the
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
