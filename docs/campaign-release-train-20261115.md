# Campaign: release train 2026-11-15

**Resume point for `/orchestrate-waves`.** Cycle per block: implement -> test -> fix -> next. Several implementation
waves run without hardware back to back; then one central bench with one bulk build on DK5EN-1, -14, -90 and -92.
DK5EN-98 (production gateway) gets soak runs only, each with an explicit operator OK.

- Started 2026-11-15 on `fork-dev`. Operator copy of the plan with the German checklist: `~/Desktop/upstream-releaseplan-20261125.html` (Fassung 12)
  (outside the repo; this doc is authoritative).
- Deadline: upstream wave 2 (Ping plus web GUI) on **Fri 2026-11-20**. It needs PING-NRF-01 and every browser row from
  B-1, so I-2 to F-1 run this week.
- Row ids are BACKLOG ids ([BACKLOG.md](BACKLOG.md) 3.2); each row carries its own bench step.

## Operator decisions 2026-11-15 (wave 0)

| Item                            | Decision                                                                                                 |
| ------------------------------- | -------------------------------------------------------------------------------------------------------- |
| DEDUP-LAP-01                    | Won't fix: the message id keeps skipping 100 per boot                                                    |
| `--setlog` on DK5EN-98          | Stays on until the ACK-AGE-01 run under load, then off (OK given)                                        |
| ACK-AGE-01 exit criterion       | No number: "clearly better than SOAK-1108" (median 6.1 s, max 24.8 s); the 8 s proposal is dropped       |
| Web GUI upstream                | Option D: split CSS/JS plus `tools/gen_web_assets.py` and `src/web_functions/web/README.md`; no CI break |
| Web GUI PR date                 | Together with Ping in upstream wave 2, 2026-11-20                                                        |
| TD-NAV-01                       | Option 2: header GPS icon opens the GPS tab, tab button hidden, indices unchanged                        |
| DEDUP-OWN-01, TD-22, WIFI-PS-01 | Already decided earlier (doc only / built / accepted)                                                    |
| Wave-2 web scope (2026-11-16)   | Ship the full `mc.js`/`mcrm.js` incl. wave-3/4 blocks; fail fast, the rollout reaches only 3-5% at first |
| CSMA-03 loop poll (2026-11-16)  | Keep it in `b3b2bf0b`                                                                                    |
| `web_setup.cpp` (2026-11-16)    | Take the table-driven rewrite plus WEB-LATLON-01                                                         |
| WEB-DUP-01 (2026-11-16)         | Inside the wave-2 web PR, no early separate PR                                                           |
| Wave-2 timing (2026-11-16)      | Keep the plan: Ping and Web as two PRs on 2026-11-20                                                     |

## Wave log

| Wave | State                      | Result                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| ---- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0    | done 2026-11-15            | decisions above; BACKLOG clean-up `b8544fd2`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| I-1  | done 2026-11-15 `b6d0da94` | CET-AGE-LOG-01, TRACK-PING-01, AU-26, TD-NAV-01, web brightness alignment, web assets README. WEB-LATLON-01 (`39536d0d`) and the TD-CHAT-01 web control were already done. Gate stage 1,2 PASS (3812 Unity cases), six board builds                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| I-2  | done 2026-11-15            | Triage of 7 older campaign docs -> [bench-b1-20261115.md](bench-b1-20261115.md); wave-2 content list [upstream-wave2-content-20261115.md](upstream-wave2-content-20261115.md); BACKLOG RM-DUP-01, RM-LOSTREPLY-01; PWR-01, SENS-04 to field                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| B-1  | **almost done** 2026-11-15 | all rows except TD-34 and BOOT-01 OLED; [bench-b1-20261115.md](bench-b1-20261115.md); night clock run evaluated 2026-11-16 (CET-SCATTER-01 new)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| F-1  | **running**                | F-1a `afaa2f40`, F-1b `b4ae98e4`; open: RM-LOSTREPLY-01 concept, F0A0 cleanup, release, wave-2 PRs                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| F-2  | **running** 2026-11-16     | wave A: BLE F0A0 cleanup, RM-LOSTREPLY-01 and CET-SCATTER-01 concepts, `clock_drift.py` `[CET]` capture; gate stage 1,2 PASS (3829 Unity cases), nRF52 builds green. wave B: CET Part A (advisor APPROVED; nRF52 -Werror fix after the gate). wave C: TD-SKYTAB-01 side tabs (advisor APPROVED, gate PASS 3846 cases, screenshots light/dark on DK5EN-14). wave D: CET Part B time-source rank (advisor rework once, then APPROVED; gate PASS 3863 cases, seven boards). wave E: RM-LOSTREPLY-01 fix (advisor rework once, then APPROVED; gate PASS 3870 cases). Bench F-2 (evening, DK5EN-1/-90/-92 on `dc08974f` INSTRUMENT): RM-LOSTREPLY-01 recheck PASS, BLE F0A0 scan PASS, WEB-GZ-01 Firefox PASS, CLK-01 S4 with Part A+B PASS, NTP hold check PASS (`f2-*-20261116.summary`, bench-b1 doc section "F-2 bench"). Open: TD-LOCK-01 fix, release |
| U-2  | done 2026-11-19            | Upstream wave 2: Ping #1280 (5 commits incl. LAST-PONG-01 and PING-FAIL-REPEAT-01) merged by oe1kbc 2026-11-19 (`4cfa2ac2`); Web #1281 (board LED steady, pin policy + GPIO control, web GUI) merged by oe1kbc 2026-11-19 (`45f66dc7`), synced 2026-11-20 (`ffb080a5`, tree unchanged). Web PR bench found WEB-LOAD-01 and WEB-NAV-01, fixed on fork-dev `029eb2f5` (gate PASS 3871) and in #1281. DK5EN-1, -90, -98 on `029eb2f5`.                                                                                                                                                                                                                                                                                                                                                                                                                    |

## I-2 result (2026-11-15)

- B-1 working list incl. the open steps from older campaign docs: [bench-b1-20261115.md](bench-b1-20261115.md).
- CSMA-04 INSTRUMENT image on DK5EN-1, -92, -90 (all on USB); built after the bulk build, kept as files, flashed last.
- PWR-01 and SENS-04 moved to the field: no INA226 and no DHT22 on the bench; waiting for the field I2C scan and DHT22
  feedback (operator 2026-11-15).
- Wave-2 PR content: [upstream-wave2-content-20261115.md](upstream-wave2-content-20261115.md). Operator questions
  before `/submit-pr` are in that doc (trim wave-3/4 code from `mc.js`/`mcrm.js`, drop the CSMA-03 loop poll, skip
  `web_setup.cpp`, WEB-DUP-01 placement, one or two PRs).
- Checked: the fork has no BLE `node_pingcall` sanitize because BLE-SET-01-FU (`68ee6dcf`) removed the whole nRF52 BLE
  settings write path; not a gap.

## Bulk build and flash

- `tools/regression.sh --stage 1,2` green; every board env once, sequentially, `MC_AU_DEV_CHANNEL=1`.
- Flash DK5EN-1, -14, -90, -92 (replaces the PR #1277 build they still run). `identity_guard.py` first. Load `/bench`.

## B-1: central bench (TX at most 2 dBm, traffic only to TEST or own nodes)

| Node                     | Rows                                                                                                                                     |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| DK5EN-1 (Heltec V3)      | BOOT-01, BAT-05, PING-HOLD-02, PING-STOP-01, TRACK-PING-01, WEB-LED-01, CLK-01 (S2-S4), CET-AGE-01                                       |
| DK5EN-14 (T-Deck Plus)   | TD-THEME-01 (W4), TD-CHAT-01, TD-NAV-01, TD-22, TD-23, TD-27, TD-28, TD-29, TD-30, TD-32, TD-33, TD-34, BOOT-01                          |
| DK5EN-92 (T-Beam v1.2)   | TBEAM-SLEEP-01, AU-14, QA review 8.6 ESP32 part; AU-24 and AU-25 need long uptime (soak table)                                           |
| DK5EN-90 (RAK4631)       | PING-NRF-01 (wave 2 needs it), ACK-AGE-01 (nRF52), CSMA-03, BLE-SET-01, NRF-BLEDFU, WEB-LATLON-01, WEB-GZ-01, QA review 8.6 nRF52 part   |
| Browser (Chrome, Safari) | WEB-DARK-01, WEB-DARK-02, WEB-A11Y-01, WEB-LED-02, WEB-DUP-01, WEB-PWD-01, WEB-GZ-01, WEB-TD-01, RM-Q-01 (all needed for wave 2)         |
| Radio between nodes      | TXP-02, BLK-01, CSMA-01, CSMA-05, SNF-ACK-01 (optional), SB-06, CSMA-04 (INSTRUMENT image on three nodes, last), CSMA-02 (after CSMA-04) |

**From campaign docs** (triage in I-2):
[campaign-open-items-20261102.md](campaign-open-items-20261102.md),
[campaign-bench-fixes-20261106.md](campaign-bench-fixes-20261106.md) (Safeboot `/ota`, manipulated `.autoupdate`, BLE, RAK
secrets with a phone), [qa-review-fork-dev-20261025.md](review/qa-review-fork-dev-20261025.md) section 8.6,
[campaign-tdeck-heap-wifi-20261025.md](campaign-tdeck-heap-wifi-20261025.md) (soak, GUI),
[campaign-fork-hardening-20261018.md](campaign-fork-hardening-20261018.md) (LED, deep sleep),
[extended-commands-impl-plan.md](rm-gui/extended-commands-impl-plan.md) sections 6, 9, 10 (RM-DUP),
[campaign-logreport-fixes-20261101.md](campaign-logreport-fixes-20261101.md) (own beacons prio 2, ping-target clean-up).

## F-1: fix and release

- Fix B-1 findings, each with a regression test; re-test only the affected rows; BACKLOG and RESUME per wave.
- Read AU-24 before the release. `/release-firmware`; the tag must not be `v4.40a.11.14` (upstream's), no same-day `.N`.

## Parallel tracks

- **Soak on DK5EN-98** (each run with OK): ACK-AGE-01 run under load, then `--setlog off`; release image soak; GWRX-01
  count night; TX pool TTL ([campaign-tx-pool.md](campaign-tx-pool.md)); LoRa QRT night
  ([campaign-lora-qrt-20261018.md](campaign-lora-qrt-20261018.md)); DUP/OnRxDone nights
  ([rca-brief-dup-onrxdone-20261013.md](rca-brief-dup-onrxdone-20261013.md)).
- **Channel access stabilisation** (target upstream wave 7, earliest 2027-01-29): ACK-AGE-01, ACK-CANCEL-01 (concept),
  CSMA-04/02, CSMA-03 with PWR-01, PWR-02, 24 h soak 2027-01-11 to 01-19. Exit criteria: relay ACK clearly better than
  SOAK-1108 (median 6.1 s, max 24.8 s); CSMA-04 measured and slot set; CSMA-03 threshold from a new p99; 24 h soak with
  an ESP32 gateway and an nRF52 node without reboot or ring loss.
- **Upstream waves**: 2 Ping plus web GUI 2026-11-20; 3 pins page 11-27; 4 RM and mesh 12-04; 5 gateway 12-11;
  6 Auto Update and Safeboot 12-18 (only if AU-24/25 are explained); 7 channel access. ETH-04 after field confirmation.

## Known gaps carried forward

- TRACK-PING-01: after the 10 s hold the track page stays frozen until the ping time ends (no countdown, no clock).
  Widening the gate would break the 30 s text-message hold. Follow-up, not a defect of I-1.
- TD-NAV-01 nits: the hidden GPS button keeps a ~3 px stub in the tab bar; while the GPS page is active no visible tab
  is highlighted; the GPS icon's 8 px click area overlaps the WiFi icon's edge.
