# fork-neo-test: merge upstream/dev e4a2393f -- campaign plan

Status: **DONE 2026-09-25** (all waves). Approved 2026-09-25. Operator decisions: merge the tip incl. #1156;
adopt KISS; E22_XML gets KISS too (DRAM fix, see F7); one merge commit for all code; W4 and W5
now; W4 node DK5EN-1 (bench Heltec V3); after W5 push `fork-neo` (first push), NOT
`fork-neo-test`; fork-main untouched.

## Wave status log

| Wave | Content                                              | State                                                                                                      |
| ---- | ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| S    | Scouting (4 read-only scouts + own verification)     | done                                                                                                       |
| W0   | Baseline builds, `git merge`, resolve 7 hunks        | done: 8/8 baseline green; merge committed as def2dc7e                                                      |
| W1   | KISS `String` -> `char[]` port + host test           | done: kiss_frame carve, 18/18 host after advisor vectors                                                   |
| W2   | Gate: host suite, lints, 32 envs, advisor, commit    | done: host 1067/1067, selftest green, 32/32 envs, advisor APPROVED; commits def2dc7e, 71502a94, 7ba19935   |
| W3   | neo path lists, lint rule, CHANGELOG-neo, pr-history | done: path lists 262/262 + K19 444, README counts, CHANGELOG-neo 118-123, pr-history figures               |
| W4   | Bench check on DK5EN-1 (OTA), restore afterwards     | done: IS1/SN1 over BLE, KISS server-relay tap proven, --via bug found + fixed (0923e037), DK5EN-1 restored |
| W5   | Re-derive fork-neo + `gate.sh`, push fork-neo        | done: derive.sh identity empty, gate.sh 38/40 + 0 symbol deviations, fork-neo 5d8b1f11 pushed              |

## Gate log (W2)

- Host suite, 36 native envs: 1067/1067 pass. Three test stubs needed the new
  upstream signatures (`SendAckMessage` in test_udp_frame_twin, `sendMessage` in
  test_serial_command_twin and test_getextern).
- F4 regression test: FAILS with the tap removed ("Expected 1 Was 0"), passes with it.
- F5 lint check 8: 2 hard failures on the pre-merge table, 0 after.
- `sh test/golden/selftest.sh`: exit 0 after three PRE-EXISTING staleness fixes
  (all failing on unmerged b55fe5d7 too): variant-ini-effective.json (minimal
  patch, 30 envs +ARDUINO_LOOP_STACK_SIZE=12288 from 8990e94d, 3 new native
  envs, 3 filter keys -- 2 of the 36 keys come from this merge),
  u2-twin-diff-before.txt (reason text reworded by 2616ef59), drift-matrix.csv
  DR-28 asserting_test (tests renamed when DR-28 was implemented).
- Baseline b55fe5d7 (clean): E22_XML DRAM headroom 29008 B, IRAM 4028 B;
  ttgo_tbeam IRAM 4972 B.
- After the merge (clean, 30 board + 2 safeboot envs, all green): KISS costs
  1224-1240 B DRAM and 0 B IRAM on every ESP32; E22_XML 27776 B DRAM / 4028 B
  IRAM; no region below 4000 B. RAK4631 text +48 B. KISS strings in every ESP32
  image, none in nRF52; IS1/SN1/BDATE/VIACALL in ESP32 and nRF52 images.
- Advisor (Fable): APPROVED. Applied: staging, CORE += kiss_frame, K19 +=
  test_kiss_frame and producer_match_lint.py, three more ack-rewrite vectors,
  twin_stub_lint pair (registered instead of exempted: the stub is verbatim).
  Refuted: its vector (b) expectation (":rej5 :ack7" with slot 7 IS rewritten).
- Rebuilt root safeboot*.bin discarded: no safeboot input changed, the byte
  difference is ESP32 build non-reproducibility.

## Bench log (W4, DK5EN-1, 2026-09-25 19:55-20:07)

- Flashed e79d1837 over WiFi (webflash.py, 36 s). Settings loaded ("FLASH
  layout 20260724 ok").
- **KISS started by itself** ("[KISS]...server started on port 8001"): the
  feature-neighbour-matrix build stores `--nbrdebug` in node_sset4 0x10 and
  `--nbrrelay count` in 0x20 -- the bits upstream's KISS reads as enable and
  TX-allowed. See "Open decision" below.
- BLE `--info`: I + `{"TYP":"IS1","BDATE":"20260925-193817"}`. `--nodeset`: SN +
  `{"TYP":"SN1","VIA":false,"VIACALL":""}`.
- BLE `--via on`/`off` on e79d1837: SN + SN1 as JSON, no text echo, but
  VIACALL became "ON"/"OFF" -- the TG_BRETURN fall-through into the "via "
  rung. Fixed in 0923e037 (lint check 9), flashed, re-measured: VIA toggles,
  VIACALL stays "". `--via none` restored the original empty via call.
- KISS read-only client, 7 min, nothing sent: the `{CET}` frame x6AAA30A6
  arrived from the server first (GWI 19:57:47.385) and went to the client at
  19:57:47 via the re-anchored tap (F4); the RF copy 7 s later was a duplicate
  and was not delivered again. The LoRa RX tap was not exercised: no fresh RF
  text/position frame in the window, only HEY (excluded by design).
- Restored feature-neighbour-matrix 14a2b2eb (rebuilt, build 19:54:11); NBR
  lines resume. Field-run capture gap 19:55-20:07.

## W5 log

- `tools/neo/derive.sh fork-neo-test fork-neo upstream/dev`: K01 57 / CORE 195 /
  K15 9 / K16 2 / K17 1 paths; strip identity empty, safeboot images same. Old tip
  tagged `neo-backup-202609252008-fork-neo`. derive.sh leaves the checkout ON
  fork-neo, which has no `tools/` -- run gate.sh only after switching back to
  fork-neo-test (first attempt: exit 127).
- `tools/neo/gate.sh`: builds 38/40 (the two K01 upstream-baseline headroom reds:
  E22_XML DRAM 1160 B, ttgo_tbeam IRAM), symbol deviations on 0 of 8 targets.
- `git push origin fork-neo:refs/heads/fork-neo` (first push, no tracking) ->
  5d8b1f11. Both workflows `disabled_manually` beforehand; no Actions run
  afterwards. fork-neo-test NOT pushed (origin stays at b55fe5d7), by decision.

## Open decision: node_sset4 bit collision

feature-neighbour-matrix uses node_sset4 0x10 (nbrdebug), 0x20/0x40
(nbrrelay count/on), 0x80 (nbrsym off), 0x100/0x200 (nbrreport). Upstream KISS
(now on fork-main and fork-neo-test) uses 0x10 enable, 0x20 TX, 0x40 RxMeta,
0x80 auth. Any node moving between the two builds reinterprets the bits: a
feature-neighbour-matrix node flashed with fork-neo-test/fork-main opens an
unauthenticated KISS listener with TX allowed. The NBR bits must move before
feature-neighbour-matrix takes upstream/dev. Operator decision owed.

## Starting point

- `fork-neo-test` b55fe5d7, branch point upstream `80b85a5a` (v4.35t.09.20), clean tree.
- `upstream/dev` e4a2393f: 13 non-merge commits missing -- KISS/TCP #1151 (DH1FR), via
  settings SN1 #1155, build date IS1 #1156, and our own #1152 (maxv) and #1153 (web volt).
- `fork-main` merged up to 8cff4395 (84a1c18c); it lacks #1156 (IS1).
- Dry run `git merge-tree --write-tree fork-neo-test upstream/dev` = tree 5732861e:
  7 conflict hunks in 5 files; everything else auto-merges.

## Decisions (recommendation first)

1. **Merge target: upstream/dev tip e4a2393f, including #1156.** `derive.sh` builds
   fork-neo from the upstream/dev tip; a fork-neo-test merged only to 8cff4395 would fail the
   closing `strip(fork-neo-test) == fork-neo` identity gate. fork-main picks up #1156 at its
   next sync.
2. **KISS is adopted.** neo is "upstream/dev plus better code"; KISS is upstream code and
   default off (`node_sset4` 0x10). Leaving it out would make fork-neo-test delete upstream
   code and break the derivation.
3. **Merge, not rebase** (see memory: merge-not-rebase-after-upstream-squash).
4. **One merge commit carries every code change needed to compile and keep upstream's
   behaviour** (conflict resolutions + `char[]` port + re-anchored UDP tap + via rows).
   A merge commit that does not build breaks bisect. Tests, lint, path lists and docs follow
   as separate commits.

## Findings from scouting (verified by the orchestrator)

| #   | Finding                                                                                                                                                                                                                                                                                                                                                                                                                                                       | Severity                   | Where                                                                                  |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------- | -------------------------------------------------------------------------------------- |
| F1  | KISS code uses Arduino `String` methods on `aprsMessage` fields that fork-neo-test made `char[]` (R2-04): compile failure                                                                                                                                                                                                                                                                                                                                     | critical                   | `src/kiss_functions.cpp` (15 sites)                                                    |
| F2  | `sendInjectedPosition()` auto-merges with `String` assignments into `char[]` fields: compile failure                                                                                                                                                                                                                                                                                                                                                          | critical                   | `src/loop_functions.cpp` ~4368-4372 (merged)                                           |
| F3  | `sendMessage()` / `SendAckMessage()` `src_override` hunks: upstream side assigns `String` into `char[]`; `SendAckMessage` becomes `unsigned int` (header auto-merged) but the fork body is `void`                                                                                                                                                                                                                                                             | high                       | `src/loop_functions.cpp` hunks 4154, 5079                                              |
| F4  | KISS server-relay tap (f070ad50) sits in upstream's monolithic `getMeshComUDPpacket()`; fork-neo-test carved that body into `handleUdpFrame_esp32()`. Git sees a misaligned conflict in `udp_functions.cpp`; the correct resolution (ours) silently drops the tap                                                                                                                                                                                             | high (silent feature loss) | port into `src/esp32/udp_frame_esp32.cpp` near `bSrcUnconfigured` (:181)               |
| F5  | `--via on/off`: upstream now answers over BLE with `sendNodeSetting()` (SN + SN1 JSON) instead of a bare text echo. fork-neo-test hoisted these rungs into `COMMAND_TOGGLES[]` with `TG_BLE_ECHO`, so upstream's fix lands on the empty conflict side and is lost                                                                                                                                                                                             | high (silent)              | `src/command_functions.cpp:252-253` -> `TG_DIRTY_NODE`, `TG_BRETURN`, no `TG_BLE_ECHO` |
| F6  | `getBuildDate()` (IS1) sits in the conflict at command_functions.cpp:143 next to upstream's `casecmp()` + static_asserts, which fork-neo-test moved to `command_match.h`. Keep `getBuildDate()`, drop the rest                                                                                                                                                                                                                                                | medium                     | `src/command_functions.cpp:143`                                                        |
| F7  | E22_XML-DevKitC conflict. Upstream opts the board out of KISS (`DISABLE_KISS_TCP`) because its DRAM (`dram0_0_seg`, static data/bss -- not IRAM) overflows by 32 B from a 1160 B headroom. fork-neo-test's RAM reclaim left ~17.8 kB DRAM (memory figure, re-measured in W0), so the operator decision is: E22_XML gets KISS, no opt-out, if the measured headroom stays >= 4000 B (`gate.sh` floor). Keep `-D MC_DIAG=0`; `MC_CAPTURE=0` is dead in the fork | medium                     | `variants/E22_XML-DevKitC/platformio.ini`                                              |
| F8  | neo path lists already miss `src/txring_functions.cpp` (P15) and `src/web_functions/web_nodefunctioncalls.cpp`; next `derive.sh` would fail its identity gate. Pre-existing, independent of the merge                                                                                                                                                                                                                                                         | medium                     | `tools/neo/paths/*.txt`                                                                |
| F9  | `--kiss ...` rungs auto-merge as upstream if/else rungs; fork lints (`command_ladder_lint`, `toggle_table_lint`, settings/persist lints) may want them registered or hoisted                                                                                                                                                                                                                                                                                  | low (gate decides)         | `src/command_functions.cpp` ~2238-2320                                                 |

Clean, no action: #1152 and #1153 merge to a single copy (byte-identical hunks); SN1 in
`sendNodeSetting()` and IS1 in `--info` auto-merge onto the live path; no settings-layout
change (`FLASH_VERSION` 20260912, `FLASH_STRUCT_VERSION` 20260724 unchanged); nRF52 compiles
no KISS code; `lib/kiss_ax25` + `test_kiss_ax25` are self-contained and land in
`env:native_extradio`; safeboot inputs are byte-identical (no rebuild needed).
Scout reports: session scratchpad `neo-merge/scout-*.md` (to be archived with this plan).

Refuted scout claims: "KISS was meant to stay fork-main-only" (wrong -- neo tracks upstream);
"`sendInjectedPosition()` has no definition in the merged tree" (it auto-merges at 4355);
"`--via on/off` table rows are functionally equivalent" (upstream changed the BLE answer).

## W0 -- orchestrator only, no agents

1. Clean baseline builds on b55fe5d7 of the 8 lead envs (`heltec_wifi_lora_32_V3`,
   `E22-DevKitC`, `E22_XML-DevKitC`, `ttgo_tbeam`, `ttgo_tbeam_supreme`, `t_deck`,
   `t_deck_plus`, `wiscore_rak4631`): RAM/IRAM/flash figures for the same-base comparison.
2. `git merge --no-commit upstream/dev` on fork-neo-test.
3. Resolve the 7 hunks: CLAUDE.md ours; command_functions.cpp:143 ours + `getBuildDate()`
   (F6); :2576 ours + table rows 252-253 (F5); loop_functions.cpp 4154/5079 via `mcSet()` with
   `src_override` and an `unsigned int` return in `SendAckMessage()` (F3); udp_functions.cpp
   ours (F4 ported in the next step); E22_XML ini ours (F7: no `DISABLE_KISS_TCP`).
4. Port F2 (`sendInjectedPosition()`, 4 `mcSet()` calls) and F4 (UDP tap into
   `handleUdpFrame_esp32()`).
5. `grep -c '^<<<<<<<'` over the tree = 0.

## W1 -- one implementer (Sonnet), exclusive file set

- `src/kiss_functions.cpp`: port every `String` use on `aprsMessage` fields to the
  `mc_text.h` idiom (`mcSet`/`mcAppend`, `strlen`, `strstr`, `strncmp`), keeping upstream's
  behaviour byte for byte, including the ack-map rewrite of `:ackNN` / `:rejNN` and truncation
  at `MC_PAYLOAD_LEN`.
- Carve the two pure functions `ackmapRewrite()` and `buildAx25()` into
  `src/kiss_frame.{h,cpp}` (fork carve pattern, as C1/U1) and add
  `test/test_kiss_frame/` plus a `[env:native_kiss_frame]` block in `platformio.ini`.
  Test vectors: ack rewrite (ack, rej, no match, payload at max length), AX.25 build (no digi,
  with path, SSID > 15 rejected).
- No git, no other files, no hardware. Only its own native env runs pio (single pio slot).
- In parallel (disjoint files), the orchestrator writes the F5 regression rule into
  `test/golden/toggle_table_lint.py` (`--via on/off` must carry `TG_DIRTY_NODE`, never
  `TG_BLE_ECHO`) and a stub counter for `queueKiss()` in `test/test_udp_frame_twin` that
  fails without the F4 tap. Orchestrator pio runs wait for the implementer.

## W2 -- gate, then commit

1. `ls` of all new files; zero conflict markers.
2. Host suite: all 35 `[env:native*]` envs in one pio process (the 12-env list in
   `release-firmware.md` is stale); `sh test/golden/selftest.sh`.
3. Board build: the 30 `default_envs` + 2 safeboot, one process, safeboot last.
4. Positive checks in the artifact: `KISS/TCP` string present in the Heltec V3 and
   E22_XML-DevKitC ELFs, absent in nRF52; `IS1` and `SN1` present in ESP32 and nRF52 images.
5. RAM/IRAM of the 8 lead envs against the W0 baseline (clean, sequential builds).
   Expectation: about +1.2 kB DRAM per ESP32 board; E22_XML DRAM headroom >= 4000 B (else
   fall back to upstream's opt-out and report); `ttgo_tbeam` IRAM unchanged (KISS costs 0
   IRAM since df975064).
6. Advisor pass (`fable-review`, advisor mode, Fable) on the merge diff against F1-F9.
7. Commit: merge commit (all code), then separate commits for the tests/lint.

## W3 -- tooling and docs (orchestrator)

- `tools/neo/paths`: add `src/txring_functions.cpp`, `src/web_functions/web_nodefunctioncalls.cpp`
  (F8), the new `src/kiss_frame.{h,cpp}` and every fork-changed KISS file; test paths into
  `K19.txt`. Cross-check against `git diff --name-only upstream/dev fork-neo-test`.
- `docs/CHANGELOG-neo.md`: new base (e4a2393f) and a short entry for the merge.
- `docs/presentation/pr-history.html`: "Basis" and the diffstat, recomputed.
- Memory: fork-neo-test merge state.

## W4 -- bench on DK5EN-1

Bench Heltec V3, now DK5EN-1 (gateway on, WiFi). Its serial port belongs to the running
`serial_capture.py` (`~/meshlog/dk5en-1/`): read the log, flash via `tools/webflash.py <ip>`,
never open the port. Afterwards restore the feature-neighbour-matrix build 14a2b2eb and its
settings; the field-run capture gap is noted. DK5EN-98 stays untouched. `--info` answers I + IS1 over BLE; `--via on`/`off` answers SN + SN1 JSON and no
chat text; `--kiss on` + a TCP client on port 8001 receives received RF frames (RX only, no
KISS TX, no broadcast). Settings restored afterwards.

## W5 -- fork-neo

`tools/neo/derive.sh fork-neo-test fork-neo upstream/dev`, then `tools/neo/gate.sh`
(40 builds, expected 38/40). Then the first push of `fork-neo` to origin, without tracking;
before the push, confirm that the Actions workflows on DK5EN/MeshCom-Firmware are still
`disabled_manually` (fork-neo carries upstream's `.github/` unchanged, and `ci-build.yml`
fires on a push to any branch).

## Not included

No push of fork-neo-test (operator). No merge into fork-main (it still lacks #1156) or
feature-neighbour-matrix. No upstream PR.
