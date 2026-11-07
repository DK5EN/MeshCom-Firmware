# RESUME journal: entries moved out on 2026-11-07

Verbatim from docs/RESUME.md. Labels such as "not released" are as written at the time; every commit they name was in `v4.40a.11.06` when these entries were moved.

- **Store node echo fix 2026-11-06 (`SNF-ECHO-01`), upstream PR [#1257](https://github.com/icssw-org/MeshCom-Firmware/pull/1257) merged 2026-11-06 (`649b5b17`):** shipped in `v4.40a.11.06`. Synced 2026-11-07, worktree and branch `pr/snf-echo` removed. Field check still open: `refr=` on the affected store node. DK5EN-1 now runs a local `fork-dev` build of `42941ab2` (23:28:19), no longer the CI image of `44075ef3`. After the merge: `/sync-upstream`, then remove the worktree and branch.

- **Release gate 2026-11-06, closed with `v4.40a.11.06`:** `docs/campaign-release-gate-20261106.md` holds the wave plan, decisions and state. T-Beam heap (`AU-23`) fixed in W1; `AU-22`, `EXT-PEER-01`, `WEB-PIN-01` and the `TD-34` icon fixed in W2; `RX-TX-02` settled in W3, no firmware loss; `fork-dev` pushed and CI images of `44075ef3` on all four bench nodes in W4; W5 bench pass done, `WEB-GRID-01` fixed, `BLE-SET-01` filed. Next: W6 soak, then the release decision.

- **Bench fixes and features 2026-11-06, not released:** `docs/campaign-bench-fixes-20261106.md` (W1-W5, `408657be` to
  `e7a6fbeb`). Bench images on all four nodes are INSTRUMENT builds of `e7a6fbeb`; nodes restored from the vault. DK5EN-90 is
  on `.66`. Still needs CI images: soak, Safeboot `/ota` page, manipulated `.autoupdate`, BLE, RAK secrets with a real phone.

- **`v4.40a.11.01` released 2026-11-01** (Latest, tag on `47f339ad`): RM1 `upd chan prod|dev`, ping target lock, own
  beacons at prio 2, RX-silence logging (nRF52 and ESP32), `--gateway lab`, E290 OTP without periodic full refresh.
  **Built by CI** (run 41333106863, green). Gate stage 1,2 PASS (85 envs, 3262/3262, golden 48, pytest 1473). Assets:
  68, 28/28 `.autoupdate` re-inflate after download (`78 da`), tag string present, T-Deck field-command scan ok
  (`mute=2 stat=1 udplog=1 injectraw=0`). Idle heap DK5EN-92: 68688 B (baseline 69332 B, -644 B), pass. Web flasher
  `check` 31 boards, 0 mismatches, `flash/v4.40a.10.30` pruned (gh-pages `70c0c648`). The repo-root `safeboot.bin` and
  `safeboot-s3.bin` are now the published CI build (the committed ones were stale: `settings_schema.cpp` and
  `esp32_flash.cpp` are part of the Safeboot image). **Not done:** no bench node besides DK5EN-92 (local build of the
  same commit) runs the release; own beacons at prio 2 and the boot-time ping-target clean-up are host-tested only.

- **Campaign open-items-20261102 (cloud session, branch `claude/blissful-hamilton-vd8mbd`, not released):** resume point
  [campaign-open-items-20261102.md](campaign-open-items-20261102.md). W0 BOOT-01 + UCH-01, W1 TD-26, TD-27/28/29,
  WEB-DARK-01, WEB-A11Y-01, BAT-05, TXP-02 W2 TD-30, BAT-04b, WEB-PWD-01 and W3 WEB-LED-01
  are done (host gate + builds). Next: the operator bench checks listed in the campaign doc; new items PF-T1, MCC-T1,
  WEB-DARK-02, TD-31, TD-32. All bench checks are listed in the campaign doc for the operator.

- **TD-25 / TD-24 T-Deck fixes (2026-11-02, fork-dev, not released):** the message input on T-Deck/Plus and
  T-Deck Pro caps UTF-8 bytes like the web GUI (149, minus callsign + 2 for a DM), cuts on a code-point boundary
  and shows "N bytes left" (`src/msg_input_budget.h`, test `test_msg_input_budget`). The first bubble of a new
  conversation removes the "No messages" hint. Opus advisor APPROVED. Bench on DK5EN-14 (label layout,
  150 x `ż`, DM from a new peer) passed 2026-11-02 (operator). Upstream in
  [#1249](https://github.com/icssw-org/MeshCom-Firmware/pull/1249) (merged 2026-11-02). **Open:** T-Deck Pro not benched; new TD-26 (Pro
  sends a 9-character call as a broadcast).

- **CHARS-01 national characters end to end (2026-11-02, fork-dev, not released):** one feature across every
  display path, both back ends and the web GUI. sp2ong's E290 patch (`6eda5f8a`, #1221) is the origin; on top: a
  shared fallback `src/text_fold.{h,cpp}` used by every display (letters the font lacks fold to ASCII, `ä` -> `ae`,
  `ł` -> `l`, `€` -> `EUR`), `utf8ascii()` removed, OLED in Latin-1 (real `ä é ñ`), E290 +69 glyphs (FR/ES/PT,
  punctuation), T-Deck Latin fonts (`23e03fed`) plus fallback for the rest, web GUI and T-Deck no longer cut a
  message at the first `{`. Back ends: MCProxy `6df61da`, mc-chat `bb2ae3b` (same charset policy both ways,
  150-byte checks in bytes, weather contract in bytes). Firmware W1 `6b94c253`, W2 committed after it; gate stage
  1,2 PASS (88 envs, 3324/3324), Opus advisor on every wave. Follow-ups W2b closed: E290/WP status lines per
  font, firmware filter strips U+2066-2069, MCProxy drops inbound text empty after the filter. DK5EN-14 bench
  passed 2026-11-02 (operator). **Upstream PR [#1249](https://github.com/icssw-org/MeshCom-Firmware/pull/1249)**
  (2026-11-02, closes #1221; sp2ong's commit first with him as author; 33 board envs build, scope suites green).
  Merged upstream 2026-11-02 and re-synced (`7296aaf6`). **Open:** no OLED, T-Deck Pro, T5 or E290 bench in the
  fork (E290 tested by sp2ong). Campaign:
  [campaign-chars01-e2e-20261102.md](campaign-chars01-e2e-20261102.md).

- **RM1 `upd chan prod|dev` (2026-10-31, fork-dev `636e681b`, not released):** remote update-channel switch
  DEV/MAIN for McApp. One setter `auSetChannel()` for RM1, `--updchan` and `/setparam updchan`; a switch clears the
  last check result, keeps a staged image, starts no check; console switch during a check answers busy. Flag builds
  only (`MC_AU_DEV_CHANNEL`). Gate stage 1,2 PASS (85 envs, 3219/3219), two advisor rounds. Benched DK5EN-92 ->
  DK5EN-1 over LoRa and over the 2323 console; both nodes run `636e681b`, DK5EN-1 back on `dev`. McApp work order:
  MCProxy B14 (`doc/2026-10-31_1000-rm1-updchan-requirements.md`, commit `786099a`). Campaign:
  [campaign-rm1-updchan.md](campaign-rm1-updchan.md).

- **Upstream sync 2026-10-31** (merge `a1a31850`): #1243, #1244 and #1245 squash-merged upstream, merged into
  `fork-dev` with every path on the fork side (`platformio.ini` kept: the auto-merge would have duplicated
  `[env:native_web_auth]`); the merge tree equals the pre-merge tree. Branches `pr/webui-login`,
  `pr/extudp-webui-echo`, `pr/hey-upload-receiver`, `docs/pages-tone`, `feature/td23-brightness` and their worktrees
  removed locally and on origin; `claude/fork-dev-code-review-xdxqzv` (19 commits, no PR) kept. Pages PR history:
  round 8 with the three PRs, 66 merged, none open.

- **`v4.40a.10.30` released 2026-10-30** (Latest, tag on `b3d7da3a`): WEB-AUTH-01 web login, UDP-05 EXTUDP echo,
  HEY-UP-01 gateway call in the HEY upload, E290 OTP image from #1240. **Built locally** and published with
  `gh release create`: the CI run 41121057432 stayed queued (runner not picked up) and was cancelled on operator
  request. Gate stage 1,2 PASS (84 envs, 3111/3111, golden 48, pytest 1422). Assets: 68, 28/28 `.autoupdate`
  re-inflate after download, tag string present, T-Deck field-command scan ok. Web flasher `check` 31 boards, 0
  mismatches, `flash/v4.40a.10.25` pruned; Pages index card and PR history updated (gh-pages `ed750e6d`).
  **Not done:** idle-heap step (DK5EN-92 not on USB, not on the LAN; static +672 B for the web-auth tables only),
  no bench node flashed with the release image yet.

- **WEB-AUTH-01 (#1241) fixed and bench-verified 2026-10-30** (`9ac4f45f`, not released): Enter submits the web
  login, the field gets the focus, the password no longer crosses the wire (nonce + HMAC-SHA256 in the browser),
  a random session cookie replaces the per-IP login, plaintext `/?nodepassword=` is refused. Gate stage 1,2 PASS
  (84 envs, 3111/3111), two Opus advisor rounds APPROVED after rework. Bench: DK5EN-1 (USB) 19/19 HTTP checks incl.
  throttle + Chrome (Enter, autofocus, wrong-password message, logout); DK5EN-98 (OTA, operator OK) 17/17 + Chrome.
  Both nodes run `9ac4f45f`, web password removed again (DK5EN-98 had none; DK5EN-1 no longer INSTRUMENT).
  Upstream [#1245](https://github.com/icssw-org/MeshCom-Firmware/pull/1245) (branch `pr/webui-login`, worktree
  `../MeshCom-PR-webauth`, one commit `fc388770`, firmware + `test_web_auth` + jsdom test). Details:
  [campaign-webui-login.md](campaign-webui-login.md).

- **UDP-05 fixed and bench-verified 2026-10-30** (`2b1a9dc4`, not released): EXTUDP sends now show in the node's
  own WebUI chat (DK5EN/map#3). Gate stage 1,2 PASS (83 envs, 3084/3084), golden 48, Opus advisor APPROVED after
  one rework (serial is already echoed). DK5EN-1 runs the INSTRUMENT build of `2b1a9dc4`. Upstream
  [#1244](https://github.com/icssw-org/MeshCom-Firmware/pull/1244) (branch `pr/extudp-webui-echo`, worktree
  `../MeshCom-PR-extudp`, one commit, 2 files); DK5EN/map#3 answered and closed.
  Details: [campaign-extudp-webui-echo.md](campaign-extudp-webui-echo.md), catalogue 2m.

- **HEY-UP-01 fixed and field-verified 2026-10-30** (`a3a8fc5f`, not released): a gateway's HEY upload now names
  the gateway as the last path element, so the INTERLINK feed and mcmap get the gateway-to-gateway links
  (DK5EN/map#5). Gate stage 1,2 PASS (83 envs, 3073/3073), Opus advisor APPROVED. DK5EN-98 runs the fix build
  since 17:22 (`DL2JA-2,DK5EN-98` published, mcmap edge drawn, publish rate unchanged). Upstream PR prepared in a
  parallel session. Details: [bug-hey-upload-no-receiver-20261030.md](bug-hey-upload-no-receiver-20261030.md),
  catalogue 2l. Upstream `dev` has 3 new commits (#1240, e290) for the next `/sync-upstream`.

- **DK5EN-98 extended soak of `v4.40a.10.25` passed** (2026-10-25 20:36 to 2026-10-29 18:00, 93.4 h, evaluated
  2026-10-30): finished as planned, no reboot (uptime continuous), 0 crashes, 0 `MC_SM_ERRORS`. TX pool: 0
  `RING_DROP_PRIO`, 0 `RING_DROP_NEW`, 0 `RING_DROP_STALE` (TTL and `{CET}`), queue peak 5/20, 0 retries, 0 txfail.
  Of 8512 enqueued: 5517 sent, 2925 NBR cancel (every one matched to a `[NBR]|CANCEL`), 70 superseded. Max age at
  TX far below TTL (ACK 26 s/90, text 45 s/300, position 65 s/120, HEY 99 s/180). Side finding: one WiFi AP change
  2026-10-27 05:35 (194 s without keepalive, then -67 instead of -55 dBm). Report: Desktop
  `logauswertung-dk5en-98-soak-20261025.html`, artifact <https://claude.ai/artifact/XwSCwExCxEqog8vW8yv3xo>; logs
  rpizero `~/meshlog/dk5en-98-soak-20261025/`. Details: [campaign-tx-pool.md](campaign-tx-pool.md), "DK5EN-98 soak".

- **QA review of `fork-dev` f0a81c8 and its fix campaign (2026-10-25/26, branch `claude/fork-dev-code-review-xdxqzv`, merged 2026-11-02):**
  **Rebased 2026-11-02 onto `fork-dev` `42bbe18f`** (was base `8681fcb4`; the 19 commits are now `55dcdc9`..HEAD, messages
  unchanged). Conflicts in 3 commits: `fd163e1` test_command_setters (both test sets kept), `6b25f84` web_setup table (WX-01 `uv`/`wind`
  as WSP rows, `windpin`/`windcal` stay explicit rungs, WEB-LED-01 `boardled` explicit), `591a9e7` gzip web assets (fork-dev WEB-DARK-01,
  WEB-A11Y-01, WEB-PWD-01 CSS/JS ported into `src/web_functions/web/`, `web_assets.h` regenerated, `webgui_settings_test.js` and
  `webgui_theme_test.js` read the assets). Container gate `--stage 1,2`: Unity 3440 cases, 3437 ok, 1 failed, 1 skipped (TARGET baseline
  3427/3424/1/1; the one red on both sides is PF-T1 `test_kadenz_unabhaengig`); pytest 1489 passed after one flaky
  `test_mc_console::test_nonce_without_password_names_the_key` (passes 5/5 isolated and in the full rerun; baseline 1478); node GUI
  tests same counts as TARGET (settings 85, theme 137); nRF52 link t_echo 54.2 %, T114 50.7 %, RAK4631 80.8 % flash (TARGET 85.6/82.5/88.6).
  Advisor range-diff: 16 of 19 commits identical, 3 differ only in the conflict hunks. **Local gate 2026-11-02 (closed, merged
  into `fork-dev`):** TARGET had moved to `61867a20` (docs only on top of the rebase base), merged into the branch. `--stage 1,2`
  baseline `61867a20` 3426 Unity cases 0 red, branch 3439 cases 0 red (PF-T1 does not fail on macOS; pytest 1477 to 1489, no flake this
  run). Board builds, `MC_AU_DEV_CHANNEL=1`, 35 of 36 non-native envs build on both sides (`esp32-external-radio` needs a host/port overlay,
  fails on both): flash falls in every env (ESP32 -205 to -222 KB, t_deck -298 KB, t_deck_pro -186 KB, t5_epaper -222 KB, vision-master-e290-otp
  -65 KB, safeboot -24.9 KB, RAK4631 -63 KB, T114 -256 KB, t_echo -256 KB); static RAM ESP32 +0.5 to +1.1 KB, RAK4631 -2.5 KB,
  T114 -8.4 KB, t_echo -8.7 KB. No fix needed. Table: [review/qa-review-fork-dev-20261025.md](review/qa-review-fork-dev-20261025.md) section 8.7,
  numbers in `tools/bench/runs/qa-review-local-gate-20261102.summary`. **Hardware untested**, bench list section 8.6 (DK5EN-92 / DK5EN-90).
  [review/qa-review-fork-dev-20261025.md](review/qa-review-fork-dev-20261025.md) (section 8 = result); raw reports in
  `review/qa-20261025/`. 17 work packages (WP1-WP17) as commits `52a8d5bd`..`17793944`, all cloud-built and host-tested, **not
  benched**. Fixed: QA-NET-04, QA-STAB-01/02/03/04/06, QA-NET-02 (nRF52 settings masking), QA-AU-01/05/09/11/12/15, `MC_WEB_GUARD=1`
  default plus `otaupdate`/`reboot` need X-MC, no CORS headers, size levers (`-fno-exceptions`, `CORE_DEBUG_LEVEL=0`, gzip web assets,
  nRF52 Ethernet as a per-env build flag off on T114/t_echo, T-Deck LVGL/fonts, Safeboot gzip page), DRY items. Not changed by
  decision: QA-NET-01, stack canaries. Gate: 83 Unity envs, 3021/3028 ok (known gcc-13 failures `test_kadenz_unabhaengig`,
  `native_udp_frame_twin`; a WP5 source-pin failure in `native_command_setters` fixed in the closing commit), pytest 1422, all 32
  CI envs build, `au_stage_check` 27 envs 0 refused (T-Beam family reserve now ≥371 KB). Safeboot headroom 7 KB -> 31 KB, T114/t_echo
  -244 KB. **Open:** bench list in report section 8.6 (DK5EN-92/-90), first `ci-proof-*` run for WP17, app tolerance of `bt_code` -1
  (WP4), then cut the upstream PRs from `upstream/dev`. Still open from the report: QA-SIZE-06/09/12 (sensors/GPS/OLED, no field data),
  protocol-level items (QA-RX-04/05, QA-AU-06), GET `tempoffsetoutdoor` UNKNOWN bug (needs OK).

- **`v4.40a.10.25` released 2026-10-25** (CI, Latest, tag on `61077a8c`, after two cancelled cuts: SNF-ACK-01 and upstream
  #1237 came in): TXP-01 own text sent once, WIFI-HEAP-01 raw ICMP probe (TD-21/21b), TD-20/TD-22 T-Deck heap, TD-23
  backlight (fork PR #1), SNF-ACK-01 mailbox tombstone, upstream #1237 BLE app settings; pages rework (fork PR #2).
  Gate 82 envs 2951/2951, golden 48, pytest 1410; idle heap DK5EN-92 68728 B (esp_ping variant 63884 B failed and was
  replaced). Assets: 66, 27/27 `.autoupdate` re-inflate, field-command scan ok. Web flasher `check`: 30 boards, 0
  mismatches; Pages synced. All five nodes on 10.25: DK5EN-1/-14/-92 and DK5EN-98 by the operator through the web GUI
  Auto Update (worked), DK5EN-90 by DFU from the release package.

- **`v4.40a.10.24` released 2026-10-24** (CI, Latest, tag on `c8f0be5b`): SP server host `sp.meshcom.pl` (#1236), AU-21
  tracker TFT sprite, TRK-03 T-Beam first boot GPS, client RM1 command stops its repeats on the reply (`56519299`, not
  benched). Gate 81 Unity envs 2891/2891, pytest 1352, golden 48; idle-heap step skipped (operator decision). Web flasher
  `check`: 30 boards, 0 mismatches. The first tag on `755a4dec` was moved before CI published (run cancelled for the RM fix).
  Since 2026-10-25 DK5EN-1/-90 run INSTRUMENT images of `556b997d` (TXP-01 fix), DK5EN-92 of `eb246050`
  (INSTRUMENT for `--airgap`); all three keep group 9999; DK5EN-98
  runs the `3e07f894` gate build; DK5EN-14 runs the release build of `9e4d8781`.

- **`v4.40a.10.23` released 2026-10-24** (operator chose the 10.23 date): TX pool W0-W1c, BLK-01 `--blockcall`,
  AU-18/19/20, WEB-RX-01 (#1229), RX-DEAF-01/02, GWRX-01, GWACK-01, UDP-04, BL-01 tests, upstream #1235 sync.
  Gate 80 Unity envs 2861/2861, pytest 1332, golden 48; idle heap DK5EN-92 70296 B (+964 B). Web flasher
  `check`: 30 boards, 0 mismatches. The SP host change shipped in 10.24.

- **Upstream sync 2026-10-25 (merge of `upstream/dev` 7a1cdd70):** three new upstream commits.
  - #1237 (`5123fc02`, BLE app settings): already in the fork as `ce4407b3` (same diff), nothing new.
  - #1238 (`3a9d1281`) and #1239 (`7a1cdd70`): our PRs, merged as submitted. Every path keeps the `fork-dev` side: the
    PR copies are the fork code with fork-doc references and INSTRUMENT bench hooks stripped.
  - Build config kept from the fork: `.github/workflows/meshcom-ci.yml` (`MC_AU_DEV_CHANNEL=1`, `ci-proof-*` drafts,
    `makeLatest`, `release-notes.md` body, 27-image check), `FLASH_VERSION 20261025` (upstream keeps 20260929), native
    envs in `platformio.ini`, fork `safeboot*.bin` (the published CI build; upstream carries a rebuild of the same size).
    Upstream's `[env] extra_scripts`, `-Wall -Wextra` and the S3 board `qio` were already in the fork.
  - Result: merge `7119ea2c`, `git diff ORIG_HEAD HEAD` empty. 68 conflicts (add/add from the squash), all resolved to
    the fork side. Gate: regression stage 1,2 PASS (83 Unity envs, 3066/3066), Heltec V3 and RAK4631 build.

- **Fork-only firmware upstream since #1238:** Auto Update with Safeboot v3 and the board identity marker
  (AU-13 included), RM1 remote management (#1189), SNF-GW/STOR (#1188), LoRa QRT waves 1-2, production setting lock,
  WEB-STOP-01, the AU-tied heap levers, RM LED and `chgled` override, TX pool, GWACK-01, BLK-01, TD-20..TD-23. Still
  fork-only: INSTRUMENT bench hooks, fork-doc comments, native test envs, CI fork channel. Matrix of 10-18 is
  superseded: [fork-dev-vs-upstream-20261018.md](fork-dev-vs-upstream-20261018.md).

- **T-Deck heap/WiFi campaign 2026-10-25 (TD-20, TD-21, TD-21b, TD-22, not released):** `persisted_msgs` RAM-only
  copy removed (`5b1e1da8`), WiFi IP-liveness watchdog (`5b1e1da8`, gateway-change fix `ace935c6`), 16-tab cap with
  40 KiB heap floor (`bae97fd1`); all benched on DK5EN-14. WIFI-PS-01 accepted (modem sleep). DK5EN-14 runs the
  release build of `9e4d8781`; its 24 h soak was stopped 2026-10-25 11:24 after 6 min (operator, TXP-01 bench). Open:
  a new soak after the pending T-Deck PR, GUI visual check on the device ([campaign-tdeck-heap-wifi-20261025.md](campaign-tdeck-heap-wifi-20261025.md)).

  - TX pool (TXP): W0-W1c on `fork-dev` (`cee174b7`..`eda5edb8`), benched on DK5EN-1/-90/-92. The DK5EN-98 logger
    was stopped 2026-10-25 11:24: it found **TXP-01** (own group/`*` text released before its first own TX when
    another gateway transmits the server copy first; log 50/118, bench 9/10). W1d reproduced it with
    `tools/bench/own_echo_bench.py` ([txp-own-msg-echo-release-20261024.md](txp-own-msg-echo-release-20261024.md)).
    W1e fixed it (`556b997d`, an own text is not released while READY), benched 0/20 released.
    Released in `v4.40a.10.25`, DK5EN-98 on it; 93-h soak passed (0 drops, 0 TTL evictions, peak 5/20).
    Open: operator decision on the TTL values (soak shows wide margin) ([campaign-tx-pool.md](campaign-tx-pool.md)).

- **2026-11-01:** 24-h multi-node log analysis: five OE3 gateways, DB0AIS and two HB9 Heltec V3, every claim
  adversarially verified; nine self-contained HTML reports in `docs/log-reports/2026-11-01-24h/` and the reusable
  pipeline `tools/logreport/`. Findings: RAK4631 auto-ping has no count limit (ESP32/RAK difference, decision
  open), own prio-4/5 beacons starve at busy nodes (age floor misses prio 4/5), OE3XIA deaf receiver (bench test
  pending), ACK relays without cancel path. Session review: LOGREP-01.
