# RESUME -- pick up here

**Resume here: campaign FORMAL-02, wave 10: evaluate the 18 h soak (rpizero: DK5EN-98/-14/-92; Mac USB: DK5EN-90; from 2026-11-28 18:00), bench part 1 done; waves 1-9 committed, full host gate green. Start a fresh session with `/orchestrate-waves` on [campaign-formal-02-20261127.md](campaign-formal-02-20261127.md), section "Now" (state, dispatch plan for wave 10, run rules). Waves 6 to 9 are committed and pushed on `fork-dev`; `fork-dev` contains all of `upstream/dev`; no upstream PR open.** Release plan and working checklist: operator's
Desktop `upstream-releaseplan-20261125.html` (Fassung 13). Active campaigns:
[campaign-ota-lowheap-01-20261122.md](campaign-ota-lowheap-01-20261122.md),
[campaign-release-train-20261115.md](campaign-release-train-20261115.md) (waves).

## Now (2026-11-27)

- **FORMAL-02 wave 9 (2026-11-27):** the gateway outage hold store moved to the heap, allocated only on the first
  gateway pass (`3c341d78`): static RAM -6,136 B on S3 boards and RAK4631, -1,016 B on classic ESP32. NimBLE F3
  (attribute values pre-allocated at 300 B, `71705b36`) on every ESP32 env. Phone rings on S3 and the raw LoRa log were
  dropped as candidates (no static gain, operator 2026-11-27). Gate `--changed` green.
- **FORMAL-02 wave 6 (2026-11-27):** web step 3 committed: head parser `src/web_functions/web_request.h`
  (`d528d071`) and `work_webpage` running on it (`d787c11b`, no behaviour change on the 1,247-request corpus, frozen vs
  live rig entry, whitelist empty). Proof groups `web_request` (27 jobs) and `nimble_att_write` (3 active); 13 jobs
  over 15 GB parked with a split plan (BACKLOG FORMAL-WREQ-SPLIT, FORMAL-ATTW-SPLIT). Bitwuzla instead of `--z3`.
- **Campaign FORMAL-02 (resume point `docs/campaign-formal-02-20261127.md`, section "Now"):** committed waves 1 to 4a
  (`3139a4d6` .. `acc07d31`): verifiable ACK and phone-frame parsers, settings migration, one UDP receive handler for
  ESP32 and nRF52 (operator decisions D1/D2/D3/D5), BLE queue fixes from the TLA+ model, NimBLE boundary fixes F1/F2/F6/
  F10, bounded proofs for all new cores. Since then committed: mesh relay model with loud-but-deaf nodes (`4f6ab492`;
  candidate relay rule awaits an operator decision), web wave A steps 0-2 (`28eb83bf`, `1a6c3c63`: shared hex and URL
  decoders, spectrum leak and uninitialised scan buffer fixed, request rig as baseline snapshot), NimBLE GATT write
  proofs (`d8aa2576`). Pushed, tree clean. Next: wave 6 = web step 3 (head parser `web_request.h`, live entry of the
  request rig) in parallel with the `nimble_att_write` proofs; then web waves B and C, heap work for F3, bench test.
- **Bench 2026-11-27:** DK5EN-14 runs the INSTRUMENT image of `acc07d31` (USB flash; the earlier OTA attempt failed
  because the node was in WiFi AP mode). Web request corpus captured (`tools/bench/runs/harness/webcap-dk5en14-20261127.jsonl`).
  T-Deck lag measured: the web server blocks the main loop 2.8-8 s on a weak WiFi link (BACKLOG TDECK-LAG-01).
- **Upstream issues filed 2026-11-27:** #1342 (APRS position exponent), #1343 (mobile app text length byte wraps).
- **Deferred by the operator:** power loss during an update, safeboot web proofs; NimBLE F3 done in FORMAL-02 wave 9.

## Now (2026-11-26)

- **Upstream issues for the library patch wave, 2026-11-26:** filed #1330 (NimBLE), #1331 (arduino-esp32), #1332
  (IDF mDNS/DHCP server/MTU), #1333 (DNS in the tcpip thread), #1334 (Auto Update TLS dates, TLS 1.2), #1335
  (W5100S), #1336 (Safeboot ESP32Async web lib), #1337 (UDP drain and EXTUDP queue), #1338 (APRS encoder, asset
  URL), #1339 (net console handshake), English, assigned DK5EN, neutral wording (texts: `docs/upstream-issue-drafts-20261126.md`). Net console (F2-10 plus FORMAL-NETCON-MARK) filed as #1339, worded as hardening work (operator). #1318 closed (web part in #1328, the A/W
  labels are an app topic); #1309 commented: only SB-06 left, planned with #1336. Open upstream issues of ours now:
  #1309, #1311, #1327 plus #1330-#1341.
- **Operator decisions 2026-11-26 (execbrief):** Safeboot rest goes into #1309 as a comment (gzip upload page, web
  upload drops a staged image; posted). CET-RESTAMP-01: every node with NTP or GPS with fix restamps, GPS counts, on
  by default; filed as #1340; decisions recorded in the concept. BMC jobs over 7 GB are split into slices
  (BACKLOG FORMAL-SPLIT). The release plan line "Safeboot shows MeshCom 4.4" was wrong (the Safeboot page is labelled
  the same in fork and upstream), corrected.
- **Campaign FORMAL-02 started 2026-11-27 (resume point `docs/campaign-formal-02-20261127.md`):** parsers verifiable
  (FORMAL01-SEAMS), settings round trip and migration, then BLE task model, NimBLE and the production web server, then
  a mesh model; all refactors go upstream. Wave 1 committed: `handleACK` core (`src/ack_rx_core.h`), shared id lookups
  (`src/id_table_find.h`), `readPhoneCommand(buf, len)` with `src/phone_frame.h` (drops frames whose header claims more
  than received; checked against the app source), settings migration over 25 release fixtures, nan/inf settings values
  rejected. App bug filed as #1343. Wave 2: bounded proofs for the ACK core and the phone frame decoder (`ce013112`).
  Wave 1b: the UDP receive handlers are one file `src/udp_frame.cpp` with operator decisions D1/D2/D3/D5 (nRF gateways
  show server text, server positions enter the dedup ring at once, every GATE frame stamps liveness, short datagrams
  are OTHER). Wave 3: TLA+ model `specs/tla/bletask.tla` found and the wave fixed BLE-QDROP-01 (silent queue-full
  drop) and BLE-STALE-01 (item of the previous connection applied). Next: UDP parse core proofs, wave 4 (NimBLE
  boundary, production web server; analyses `docs/nimble-boundary-20261127.md`, `docs/web-server-seams-20261127.md`
  in progress), wave 5 mesh model with asymmetric 1-2 W nodes (HB9ZG-49 measurements).
- **FORMAL01-RUN done 2026-11-27, committed `96cd34be` on `fork-dev`:** TLC `--all` 56/56 as expected, BMC `--all --heavy`
  74/74 with 0 unexpected results or tool errors (`specs/tla/RESULTS.md`, `specs/cbmc/RESULTS.md`; local, one runner
  at a time, 15 GB budget, peak 11.3 GB). Every job that ran over 15 GB or into its time limit passes after a
  verifiability refactor or a proof split (operator rule; BACKLOG FORMAL-SPLIT): B-APRS-PATH-01 (`decodeAPRS` path and
  payload as ranges, `src/aprs_path_scan.h`, FCS sum proven on its own), B-APRS-POS-01 (`decodeAPRSPOS` one tag pass,
  bounded reads), B-RM-RANGE-01 (`remote_cmd.cpp` bounded scans and explicit lengths, one strike call site in
  `rmCheck`, division-free `outU32`, `rmCanonical` proven on its own behind `RM_CANONICAL_EXTERNAL`), B-AL-RANGE-01
  (`applink_ws.cpp` payload copy). Each identical to a frozen copy of the old code (differential host tests
  `test_aprs_path_scan`, `test_aprs_pos_scan`, `test_rm_range`, `test_applink_ws_range`; old/new drivers with MeshMap,
  corpus and generated data). Found and fixed: APRS-POS-RANGE-01 (lat/lon such as `1e99` reached an undefined
  float-to-int cast; regression test red before, green after). Gate PASS (4,292 host cases). Upstream issue for
  APRS-POS-RANGE-01: [#1342](https://github.com/icssw-org/MeshCom-Firmware/issues/1342).
- **FORMAL-NETCON-MARK fixed 2026-11-26 (`96cd34be`, #1339):** mark/free only by the current holder under
  `s_hs_mux`; regression test red before, green after; `netconsole.cfg` with `MarkGuard = TRUE`, old state in
  `netconsole_was_*.cfg`. Bench with several connections open.
- **SLEEP-PULLUP-01 (2026-11-26):** `--deepsleep` with `--button off` on a board without external pull-up wakes at once
  (wake pin armed, pull-up only with `--button on`). Operator: fix with `INPUT_PULLUP` before arming; BACKLOG row and
  upstream issue #1341.
- **Upstream sync 2026-11-26:** #1328 (combined) and #1329 (GW-SRV-1324) merged upstream on 2026-11-26 without review
  changes; merged `upstream/dev` `5bca4703` into fork-dev (merge path). All 21 conflicted paths keep fork-dev; the
  auto-merge duplicated `extudp_refresh_decide()` in `src/extudp_refresh.h`, fork-dev side kept there too. Merge tree
  equals the pre-merge tree. Follow-up commit takes HOLD-NONET-01 (`UDP_HOLD_BYTES 0` on T114/T-Echo, until now only
  in the PR branch) and regenerates the macro baseline (also the stale `FLASH_VERSION` 20261122 -> 20261125 from the
  11.25 release). Worktrees `../MeshCom-PR-next`, `../MeshCom-PR-1324`, branches `pr/combined`,
  `pr/gw-server-alive-1324` (local and origin) and the old PR body files in `../` removed. Tag clash
  `v4.40a.11.22` (see the #1316 entry): operator decision 2026-11-26, keep both tags as they are; the fetch
  rejection of upstream's tag is expected, never force-fetch, move or delete either.
- **GW-SRV-1324 on `fork-dev` 2026-11-25:** upstream issue #1324 (IS0GQX-12): with the ISP down but WiFi up a gateway kept setting the "already on the server" bit on RF relays, so neighbouring gateways skipped the upload and nodes heard only by it vanished from the map. Fix: `gwLinkServerAlive()` in `gw_link_status` (a real server frame within 65 s) replaces `bGATEWAY && node_hasIPaddress` at the relay flag and at the phone "server reached" ACK. 28 cases in `test_gw_link_status`, two pin the call sites. Gate stage 1,2 PASS; Opus advisor APPROVED; bench DK5EN-14 S1 -> S0 with a silent server, S1 again once it answers (`tools/bench/runs/harness/gw1324-20261125.summary`). `--srvip` is INSTRUMENT-only. Filed upstream as [#1329](https://github.com/icssw-org/MeshCom-Firmware/pull/1329) (2026-11-26): branch `pr/gw-server-alive-1324` in worktree `../MeshCom-PR-1324`, one commit `7ce34c19` on `upstream/dev` `2dcac7d8`, src only, 33/34 board envs build (`esp32-external-radio` needs its overlay).
- **Combined upstream PR filed as [#1328](https://github.com/icssw-org/MeshCom-Firmware/pull/1328) (2026-11-25):** branch `pr/combined` in worktree `../MeshCom-PR-next`, 13 commits on
  `upstream/dev` `2dcac7d8`, 59 files, +5472/-529. Contents: Auto Update (OTA-LOWHEAP-01, AU-29, AU-30, schedule, TLS
  cap), RAM-WSP-01, RX path with QA-RX-07/02, INA226, gateway hold (always on, 0 B on T114/T-Echo), EXTUDP register
  query with EXT-PEER-01, RM pins/gpio, TD-35, TD-36, `--webhost` (#1323), `hmac_connect.py`. Closes #1303, #1305-#1308,
  #1323, #1325, #1326; refs #1309. Issues filed 2026-11-25: #1325, #1326, #1327; #1306/#1307 texts updated.
  - Gate: all 33 envs build; independent review found two porting defects (`--imax` re-apply in the wrong handler,
    blocking DNS retry per loop on ESP32 without the async resolver), both fixed in the branch; fork host suites over
    the branch fail only for out-of-scope parts. Bench with the branch: DK5EN-1 and DK5EN-92 staged the official
    11.22 from icssw-org (`tools/bench/runs/harness/pr-combined-bench-20261125/trigger.summary`); Safeboot then installed
    it at the next OTA call (the branch has no `--update unstage`); both restored to 11.25.
  - Push and file: `git -C ../MeshCom-PR-next push -u origin pr/combined`, then
    `gh pr create -R icssw-org/MeshCom-Firmware --base dev --head DK5EN:pr/combined --title "Auto Update ohne Speicherfehler, Gateway-Halten, Registerabfrage, kleine Fixes" --body-file docs/pr-combined-body-20261125.md`.
  - Fork follow-up: UDP_HOLD_BYTES 0 on T114/T-Echo exists only in the PR branch (BACKLOG HOLD-NONET-01).
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
