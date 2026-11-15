# Campaign: release train 2026-11-15

**Resume point for `/orchestrate-waves`.** Cycle per block: implement -> test -> fix -> next. Several implementation
waves run without hardware back to back; then one central bench with one bulk build on DK5EN-1, -14, -90 and -92.
DK5EN-98 (production gateway) gets soak runs only, each with an explicit operator OK.

- Started 2026-11-15 on `fork-dev`. Operator copy of the plan as a German checklist: `~/Desktop/MeshCom-Checkliste-20261115.md`
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

## Wave log

| Wave | State                      | Result                                                                                                                                                                                                                              |
| ---- | -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0    | done 2026-11-15            | decisions above; BACKLOG clean-up `b8544fd2`                                                                                                                                                                                        |
| I-1  | done 2026-11-15 `b6d0da94` | CET-AGE-LOG-01, TRACK-PING-01, AU-26, TD-NAV-01, web brightness alignment, web assets README. WEB-LATLON-01 (`39536d0d`) and the TD-CHAT-01 web control were already done. Gate stage 1,2 PASS (3812 Unity cases), six board builds |
| I-2  | **next**                   | see below                                                                                                                                                                                                                           |
| B-1  | open                       | central bench                                                                                                                                                                                                                       |
| F-1  | open                       | fixes, re-test, release                                                                                                                                                                                                             |

## I-2: bench preparation (no hardware writes)

- CSMA-04: build the INSTRUMENT image for the three-node measurement (separate from the bulk build). Recipe in the
  CSMA-04 row; INSTRUMENT flashing per the `/bench` skill.
- PWR-01: plan the INA226 current measurement on DK5EN-90 (relay-only config: no GPS, no display, BLE off).
- Triage the bench lists of older campaign docs (below, "From campaign docs"): mark each step done or still open in its
  own doc, so B-1 runs only open steps. Read-only work, fits `scout` agents.
- Prepare the upstream wave-2 branch content list (Ping commits `b3b2bf0b`, `5583b177`; web GUI with option D) for
  `/submit-pr`; the PR itself is cut after B-1.

## Bulk build and flash

- `tools/regression.sh --stage 1,2` green; every board env once, sequentially, `MC_AU_DEV_CHANNEL=1`.
- Flash DK5EN-1, -14, -90, -92 (replaces the PR #1277 build they still run). `identity_guard.py` first. Load `/bench`.

## B-1: central bench (TX at most 2 dBm, traffic only to TEST or own nodes)

| Node                     | Rows                                                                                                                                           |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| DK5EN-1 (Heltec V3)      | BOOT-01, BAT-05, PING-HOLD-02, PING-STOP-01, TRACK-PING-01, WEB-LED-01, CLK-01 (S2-S4), CET-AGE-01                                             |
| DK5EN-14 (T-Deck Plus)   | TD-THEME-01 (W4), TD-CHAT-01, TD-NAV-01, TD-22, TD-23, TD-27, TD-28, TD-29, TD-30, TD-32, TD-33, TD-34, BOOT-01                                |
| DK5EN-92 (T-Beam v1.2)   | TBEAM-SLEEP-01, AU-14, QA review 8.6 ESP32 part; AU-24 and AU-25 need long uptime (soak table)                                                 |
| DK5EN-90 (RAK4631)       | PING-NRF-01 (wave 2 needs it), ACK-AGE-01 (nRF52), CSMA-03, BLE-SET-01, NRF-BLEDFU, WEB-LATLON-01, WEB-GZ-01, PWR-01, QA review 8.6 nRF52 part |
| Browser (Chrome, Safari) | WEB-DARK-01, WEB-DARK-02, WEB-A11Y-01, WEB-LED-02, WEB-DUP-01, WEB-PWD-01, WEB-GZ-01, WEB-TD-01, RM-Q-01 (all needed for wave 2)               |
| Radio between nodes      | TXP-02, BLK-01, CSMA-01, CSMA-05, SNF-ACK-01 (optional), SB-06, CSMA-04 (INSTRUMENT image on three nodes, last), CSMA-02 (after CSMA-04)       |
| Needs a sensor           | SENS-04 (DHT22 on the one-wire pin)                                                                                                            |

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
- **Channel access stabilisation** (target upstream wave 7, earliest 2027-01-29), dates and exit criteria in
  [release-plan-upstream-20261114.md](release-plan-upstream-20261114.md): ACK-AGE-01, ACK-CANCEL-01 (concept), CSMA-04/02,
  CSMA-03 with PWR-01, PWR-02, 24 h soak 2027-01-11 to 01-19.
- **Upstream waves**: 2 Ping plus web GUI 2026-11-20; 3 pins page 11-27; 4 RM and mesh 12-04; 5 gateway 12-11;
  6 Auto Update and Safeboot 12-18 (only if AU-24/25 are explained); 7 channel access. ETH-04 after field confirmation.

## Known gaps carried forward

- TRACK-PING-01: after the 10 s hold the track page stays frozen until the ping time ends (no countdown, no clock).
  Widening the gate would break the 30 s text-message hold. Follow-up, not a defect of I-1.
- TD-NAV-01 nits: the hidden GPS button keeps a ~3 px stub in the tab bar; while the GPS page is active no visible tab
  is highlighted; the GPS icon's 8 px click area overlaps the WiFi icon's edge.
