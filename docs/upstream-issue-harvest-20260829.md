# Upstream issue harvest — icssw-org/MeshCom-Firmware, all 355 issues

**Date:** 2026-08-29 · **Source:** `gh issue list -R icssw-org/MeshCom-Firmware --state all` (355 issues,
11 open / 344 closed, #32 … #1111) · **Read:** title, body and every comment thread.

**Question asked:** are there issues — open or closed — for RAK4631/nRF52, Heltec V3, T-Deck and
T-Beam that we can harvest into our backlog? For closed ones: was there ever a report back that it
was actually fixed, or was the thread just closed?

---

## 1. What the corpus actually is

| Slice                                          | Count | Harvest value                                                                                           |
| ---------------------------------------------- | ----- | ------------------------------------------------------------------------------------------------------- |
| Filed by `karamo` (OE3WAS)                     | 201   | Mostly self-filed code nits with the patch attached, closed same day. Low. ~15 carry real observations. |
| Filed by users / other devs                    | 154   | **This is the gold.** Field reports, several never reproduced or never verified.                        |
| Closed with an explicit "done/fixed in vX"     | 226   | Confirmed fixed by the maintainer, mostly not re-confirmed by the reporter.                             |
| Closed with **no fix statement at all**        | 104   | Where the harvest is.                                                                                   |
| Closed with an explicit "won't do / by design" | 14    | Do not re-raise upstream — see §6.                                                                      |
| Open                                           | 11    | Two matter to us (#1110, #1111), see §3.                                                                |

`stateReason` is useless here: GitHub marks all 344 as `COMPLETED`; nobody uses "not planned". The
only way to tell a fix from a dismissal is the comment thread, which is why this pass read them.

**Reporter-confirmed fixes are rare.** Across 344 closed issues there are roughly five where the
reporter came back and said it works: #831, #639, #647, #562, #118. Everything else is the
maintainer's word. Treat "done with v4.35x" as "believed fixed", not as evidence.

---

## 2. Corroboration of what we already have

The strongest result of this pass. These are independent field reports of defects we found by
reading code and by bench measurement. They are worth citing when the fixes go upstream.

| Our ID                                                             | Upstream                                     | What the field adds                                                                                                                                                                                                                                                                                                                           |
| ------------------------------------------------------------------ | -------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **TM-24** (no roaming, BSSID pinned)                               | **#87**                                      | OE1KBC's own report: AP with two SSIDs, hidden SSID → the node loops `WiFI ssid<Service> connection error` forever. DK9BT: "das Problem kommt von der Geschichte mit dem **Scan** wenn man mehrere SSIDs hat". Same root as TM-24: scan-then-pin. Patched narrowly (PR #89), the class was never addressed.                                   |
| **TM-24**                                                          | **#94, #91** (DK9BT)                         | "WIFI blocks MainLoop" and "WIFI needs connection monitoring" — both closed 2025-02-24 with _"This error is no longer reported in the current versions"_. **No fix was ever made.** These are TM-20 and the `checkWifiPing` watchdog, filed 18 months before us and closed on silence.                                                        |
| **TM-24 / TD-01**                                                  | **#639** (rocktester, aendes)                | 10 hours of `[WIFI]..Reconnecting to WiFi...` every 5 s with `wifi:timeout when WiFi un-init, type=4` and `WiFiUdp endPacket(): could not send data: 118`. Two different board types, same location; problem disappears at a different location. Reporter confirms 4.35h improved it. Site-dependent → AP-behaviour-dependent, exactly TM-24. |
| **TM-24**                                                          | **#559** (dg9ffm), **#553**                  | No reconnect after AP loss, webserver unreachable until reboot (Heltec V2 + T-Beam). Dismissed with _"Dauert immer einige Minuten"_. #553: WLAN dropped after an MHeard query — that one was real and fixed in 4.35c.                                                                                                                         |
| **TD-01**                                                          | **#228** (DL4QB)                             | Node cannot join a Raspberry-Pi hotspot every other client joins; reporter measured **25 s** to association. "Solved on next version" — no detail. Our first-join failure with a patient-retry fix is the same shape.                                                                                                                         |
| **TD-01 / TM-16**                                                  | **#207, #203** (DL4QB)                       | Boot without WiFi → display stuck on "Starting now", and **no IP shown even after WiFi returns**. Answered as by-design (39 s wait, retry every 15 min). The second half — stale IP display after a late join — was never addressed.                                                                                                          |
| **TM-11 (bench)**                                                  | **#1059** (dantinca, T-Beam v1.2)            | Undocumented: **WiFi only connects when webserver, gateway or netconsole is on.** Cost the reporter a full thread. Matches our own bench pitfall.                                                                                                                                                                                             |
| **TM-09 / TM-22**                                                  | **#51, #394** (dl9sec, karamo)               | dl9sec proved in 2024 that Heltec V3 works on **hardware** I2C with the full frame buffer ("sehr flott") and that SW-I2C was the reason `--showI2C` and BME280 were broken; karamo reported the same slowness on ESP32-S3. Our TM-09 measurement (579 → 34.5 ms) is the number that thread never had.                                         |
| **TM-22**                                                          | **#52, #433, #227**                          | T-Beam OLED: wrong u8g2 driver → pixel columns on the left edge (#52, fixed by PR); SSD1306/SH1106 constructor mapping swapped for `BOARD_TBEAM_V3` (#433); **a display is "detected" on a T-Beam with no display attached** (#227) — `--info` then reports DISPLAY on over BLE.                                                              |
| **TM-10**                                                          | **#458** (karamo)                            | The SSD1306-vs-SH1106 probe reads undocumented status-register bits and is "höchst fragwürdig und rein empirisch". Closed 2026-04-14 with no change. Any dirty-flag/partial-update work sits on top of this guess.                                                                                                                            |
| **N-12** (flash layout)                                            | **#661** (m-ugo, RAK4631)                    | A low-battery brown-out corrupts stored settings and the firmware then crashes on boot; **the only recovery on a RAK4631 is formatting the flash and reflashing**. Upstream answer was a new `--cleanflash` command — which needs a working serial console, i.e. it does not help a bricked remote node.                                      |
| **N-12**                                                           | **#57** (dl7ata)                             | Same class from the other end: typing `1` into RF_POWER makes the node unreachable over LAN, serial _and_ BLE. Patched by clamping TX power to ≥2 dBm (#140), not by validating persisted values at load.                                                                                                                                     |
| **BLE `I` register** (`docs/issue-ble-i-register-mtu-20260828.md`) | **#1110 — OPEN**                             | dantinca, 2026-08-28: config info (callsign, groups) renders correctly over BLE on `08.27`, **corrupted on `08.28.stable`**. That is our `FWDATE` truncation, reported by a user the same day, with `--bledebug` and `--info` logs attached and **still unanswered**. See §3.                                                                 |
| **`bug-N25-gps-baud-scan-watchdog.md`**                            | **#875** (karamo)                            | With **no GPS module attached**, the firmware still "detects" 38400 baud, reports no module type, and emits GPSDEBUG lines for a device that is not there. Marked done for "the next release" — worth re-verifying on current dev.                                                                                                            |
| **PR #1102 (merged)**                                              | **#1111 — OPEN** (m-ugo, RAK4631)            | Telemetry stops after ~2 months uptime; reporter diagnosed the 32-bit `millis()` rollover at 49.7 days himself. Already fixed by our PR. **Close the loop:** it is still open and the fix is unreleased.                                                                                                                                      |
| Ringbuffer / message loss (our #708/#713 work)                     | **#107, #504, #568, #154, #565, #217, #204** | Six independent "messages stop / are not all forwarded" reports over 18 months. See §4.2.                                                                                                                                                                                                                                                     |

---

## 3. Open issues that concern us right now

| #                                        | Board          | State                                                                                                                                                                                                                                        |
| ---------------------------------------- | -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1110**                                 | TTGO Lora32    | **Almost certainly our `I`-register truncation.** BLE path is board-independent, so it hits T-Deck/Heltec/T-Beam/RAK identically. Verify against the attached `--bledebug on.txt`, then answer the thread pointing at our analysis document. |
| **1111**                                 | RAK4631        | `millis()` rollover, fixed in merged PR #1102, awaiting a release. Nothing to do but say so — already answered.                                                                                                                              |
| **962**                                  | T-Beam, Heltec | `--deepsleep` rework, open since 2026-05-27. Contains a real defect: **after a brown-out, modules on a slowly-rising solar supply do not restart on their own and need a manual RESET.** Also proposes light-sleep with LoRa-RX wake.        |
| 1076, 1073, 1071, 999, 224, 174, 106, 97 | —              | Feature requests, no defect content for us.                                                                                                                                                                                                  |

---

## 4. New candidates for our backlog

Ordered by value. Each is a defect that upstream closed **without a fix**, on hardware we own.

### 4.1 T-Deck Plus goes unusable after ~10 minutes — #1083 (dl9sec, closed "not reproducible")

> "Mit Firmware v4.35p.08.10.2 und v4.35p.08.06 reagiert nach einigen (zehn) Minuten der
> Touchscreen nur noch mit mehreren 10s Verzögerung. Nicht mehr sinnvoll nutzbar. Webinterface und
> OTA funktioniert normal. Mit Firmware v4.35p.07.26 funktioniert alles normal."

Closed the next day with _"leider nicht nachvollziehbar"_ and _"evtl. einmal mit ERASE neu flashen"_.

Why this matters: **web interface and OTA keep working while the UI does not.** That is not a hang,
it is the loop task being starved — the exact failure mode TM-01…TM-05 describe, seen in the field by
a competent reporter (dl9sec also filed the two u8g2 fixes). And he handed over a **version bisect
window for free: `4.35p.07.26` good, `4.35p.08.06` bad.** We have a T-Deck Plus on the bench, a loop
instrument and a harness. This is the cheapest reproduction we will ever be given.

### 4.2 LoRa RX/TX stops silently after hours to days — #504, #639 (Heltec V3), closed "nothing found"

> "The LORA RX/TX sometimes stops working without an error message and the module needs a reboot.
> But WIFI and BLE working ok all the time." — rocktester (DG0OPK), Heltec V3, three weeks of testing

Closed 2025-07-21 with _"nothing found .. sorry"_. Related, all closed without a root cause: #154
(only 1–2 messages send from the web UI, then nothing until reboot, **zero comments**), #565, #107,
#217, #204/#213 (a gateway that loses WiFi stops being gated by its neighbours; reporter said 4.34r
did not fix it, thread closed anyway).

**Verdict on the vague ones: accept as-is.** #504 is "sometimes, after weeks" with no log; #154 has
no comments and no log. If our stability PR fixed them, nothing in either thread could tell us, and
neither gives a bench experiment. DG0OPK (#504) is the same operator whose 48 h capture is already in
our corpora — that capture, not the issue, is the way in if we ever want it.

**#568 is different — it has evidence, and it survives.** The attached gateway log
(`user-attachments/files/21353203`, still downloadable, 161 records) contains the exact packet the
reporter named. Pulled and analysed 2026-08-29:

| msg_id     | arrived  | gap     | msg bytes |
| ---------- | -------- | ------- | --------- |
| `A265901D` | 14:22:37 | —       | 127       |
| `A265901E` | 14:22:45 | 8 s     | 126       |
| `A265901F` | 14:22:52 | 7 s     | 128       |
| `A2659020` | 14:23:03 | 11 s    | 128       |
| `A2659021` | 14:23:07 | **4 s** | 128       |
| `A2659022` | 14:23:15 | 8 s     | 128       |
| `A2659023` | 14:23:22 | 7 s     | 32        |

A BBS user listing from DB0SEP-12 to DJ8MEH-46: seven UDP messages pushed into a LoRa gateway in
45 s. **The one packet the reporter says never arrived (`A2659021`) is precisely the one that arrived
on the shortest inter-arrival gap** — and it is the same size as its neighbours, so this is a timing
effect, not a length effect. A gateway being fed faster than it can radiate at SF11/BW250 is exactly
the TX-queue-latency and dedup-overflow behaviour we already measured in the DG0OPK corpus.

Caveat stated plainly: the log records the **ingress** side only, so it does not by itself prove the
drop — that rests on the reporter's cross-check against the receiving node's web client. But it does
give an exact, cheap bench experiment: feed a gateway N equal-sized UDP messages at decreasing
inter-arrival and count LoRa TX against ingress. Filed as **TM-31**.

### 4.3 A bad persisted value bricks the node — #661 (RAK4631), #57

Two reports, same class, both closed without addressing it: a corrupted or out-of-range setting in
flash makes the node unreachable on **every** interface, and the only recovery is physical. On a
RAK4631 that means erasing the flash. #661 is what that looks like from a hilltop in winter.

**Checked against our branch 2026-08-29 — we have half of this.** The `N-12` fix in
`nrf52_flash.cpp` does validate struct _integrity_ on load: `valid_mark_1`, `MESHCOM_DATA_MARKER`
and a `stored_size != sizeof(s_meshcom_settings)` check, falling back to `flash_reset()` with a
logged reason. That covers a mangled or stale-layout struct.

It does **not** cover field plausibility. In `esp32_flash.cpp:56-60`:

```cpp
meshcom_settings.node_power = preferences.getInt("node_power", -20); // not set
meshcom_settings.node_freq  = preferences.getFloat("node_freq", 0);
meshcom_settings.node_bw    = preferences.getFloat("node_bw", 0);
meshcom_settings.node_sf    = preferences.getInt("node_sf", 0);
meshcom_settings.node_cr    = preferences.getInt("node_cr", 0);
```

Only the `-20` sentinel produces a default (`esp32_main.cpp:1339`, `:1419`). Any other stored value —
including the `1` that bricked dl7ata's node in #57 — passes straight through to
`radio.setOutputPower()`. The command path range-checks on entry; the **load path does not**, so a
value written by an older firmware, under a different validation rule, or by a marker-valid but
partly-corrupt write is never caught. Upstream patched only the narrow case (#140: clamp TX power to
≥2 dBm). Filed as **TM-32**: range-check the radio parameters at load, fall back to the board default,
log it.

### 4.4 T-Deck touch fails at boot — #64 (ddfeww), dismissed

> "Sometimes after startup, the touch-functionality doesn't work — `touch: failed` on boot. […] It
> seems that TOUCH_INT GPIO(16) is pulled on the t-deck without plus to low, but the T-Deck Plus is
> pulling it high." (with a link to `Xinyuan-LilyGO/T-Deck` issue #42)

Closed within hours with _"this GITHUB is not for T-DECK Hardware"_ — and then, in the same thread,
the maintainer concedes: _"Das mit dem Touch-Screen habe ich auch wenn der AKKU schwach ist. Ich kann
damit leben."_

**Still present on our branch** (`tdeck_main.cpp:189`): `touch.setPins(-1, TDECK_TOUCH_INT)` — no
reset pin — followed by a single `touch.begin(Wire)`. One failure sets `bTouchDected = false` and
touch is dead for the rest of the session; there is no retry and, without a reset line, no way to
force GT911 address selection. That is a plausible mechanism for the reporter's race. Cheap fix to
try: retry `begin()` a few times, or drive INT during reset.

### 4.5 T-Deck GUI defects closed without verification — #690 (karamo)

Four items, self-closed with _"Verbesserungen wurden durchgeführt und ich schließe mal hier ab"_ — no
verification, no version. **Two of them are still true on our branch, verified by reading
2026-08-29:**

- **`--display on / off` over the terminal has no effect — confirmed.** `command_functions.cpp:873-908`
  sets `bDisplayOff` / `bDisplayIsOff` and calls `sendDisplayHead()`; that is the U8g2 path.
  `grep bDisplayOff src/t-deck/` returns **nothing** — the TFT backlight is never touched by this
  command.
- **`SET > WIFI` shows OFF while WiFi is connected — confirmed.** `lv_obj_functions.cpp:4015-4023`
  drives the switch from `meshcom_settings.node_wifion`, the stored _intent_ flag, not the
  association state. A node whose WiFi came up because gateway/webserver/netconsole forced it
  (cf. #1059) therefore shows a wrong switch. Same family as `TM-17`.
- keyboard `SET > MODUS > LIGHT ON` has no effect (only `Alt-B` works) — not re-checked, bench item.
- APRS symbol list appears to contain only four entries because the scroll affordance is invisible —
  cosmetic, still worth a look during GUI work.

Direct check list for our T-Deck GUI review (`docs/review-tdeck-gui-20260828.md`). The `SET > WIFI`
one is a state-display bug of the same family as TM-17.

### 4.6 T-Deck Plus battery value frozen, position "outdated" — #267 (gewoem)

Dashboard shows GPS but marks it outdated; **battery value appears frozen**; a T-Beam running beside
it is fine. Closed with a "did you pick the right firmware?" question and no reply. Battery-read on
T-Deck is one command on our bench.

### 4.7 Brown-out / no self-restart on solar supply — #962 (open), #1053, #910, #940

The recurring theme in the battery threads: modules on a slowly-rising supply after brown-out do not
come back without a manual RESET, and the low-battery deep-sleep threshold is board-dependent and
repeatedly wrong (#1053: T-Beam 1W defaults to 4.2 V max instead of 8.2 V and deep-sleeps with a full
2S pack; #926: `ADC_11db` attenuation too low for 8.4 V — **closed with zero comments**). Relevant to
any of our nodes that is expected to survive unattended.

### 4.8 Smaller items worth a line each

| #                         | Board     | Item                                                                                                                                                                                                                      |
| ------------------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **332**                   | all ESP32 | `Serial.print()` **inside the `setFlagReceive()`/`setFlagSent()` ISRs** caused the recurring ISR/WDT crashes. Fixed 4.34x.04.27 — worth a regression guard, given what we know about printf and heap.                     |
| **486**                   | all ESP32 | Missing coredump partition → the bootloader emits a serial **BREAK**, which kills web-serial sessions mid-flash. Affects our bench tooling.                                                                               |
| **104**                   | ESP32-S3  | "USB serial via CDC has a very strong delay in the setup" — closed with _"any check done, on my side i can not follow the issue"_. Hits T-Deck and Heltec V3 bench work.                                                  |
| **358**, **250**, **119** | all       | Crash-by-input: 48 emoji from the web UI → reboot loop; a long multi-command line → `Stack smashing protect failure!`; a UDP message ≥125 chars → reboot. All "fixed"; all belong in the buffer-inventory regression set. |
| **485**                   | RAK4631   | RAK with an Ethernet board and **no cable** blocks serial input entirely (output still works). Fixed 4.34y.06.10 — verify it stayed fixed; DK5EN-90 is exactly this configuration.                                        |
| **582**                   | RAK4631   | 4.35c crashed on sending a message; node answered only ping afterwards. Closed with _"bei den aktuellen Tests nicht mehr nachvollziehbar"_.                                                                               |
| **499**                   | RAK4631   | CONFFIN not sent over BLE when text messages are queued — verified on ESP32, explicitly **"Not tested on RAK nodes"**, and never was.                                                                                     |
| **814**                   | T-Deck    | Debug log to SD card, rejected as "SD ist viel zu langsam". We know from TM-05/TM-07 exactly why writing to SD from the loop path is dangerous — either scope it as opt-in or record it as a documented won't-do.         |
| **1040**                  | all       | Our own `/N` truncation report; the fix reordered the beacon fields. Worth a spec test in `test/test_aprs_spec/` — nothing pins the field order today.                                                                    |

---

## 5. Coverage note on karamo's 201 issues

Skimmed in full; they are a running code review with patches attached, mostly closed within a day.
Everything of substance for our four boards is already folded into §2 and §4 (#227, #433, #458, #486,
#332, #690, #814, #875, #926, #962). The remainder is typos, format specifiers, compiler warnings,
`configuration.h` pin corrections for E22 DevKitC boards we do not build, and the T-Beam 1W bring-up,
which lives in his own repository.

One structural observation worth keeping: **the same defect classes recur across boards because the
board-specific `configuration.h` files are hand-maintained and unverified** — swapped GPS RX/TX on
T-Deck (#837), an ADC pin that collides with `PMU_IRQ` on T-Beam (#409), GPS pins that collide with
the SD card on TLORA (#876), a `LORA_DIO2` / `ANALOG_PIN` collision on E22 (#359), missing
`build_flags` in eight variants (#555). There is no pin-conflict check anywhere in the build.

---

## 6. Do not re-raise upstream

Explicit maintainer decisions. Re-filing these burns goodwill.

| #             | Decision                                                                                       |
| ------------- | ---------------------------------------------------------------------------------------------- |
| 669           | GPS power switching via NPN transistor — _"nicht am plan"_                                     |
| 69            | User-configurable forced reboot timer — _"I do not want to install a hard reset"_              |
| 68            | Syslog — declined twice, reporter pushed back, still declined                                  |
| 876           | GPS pins colliding with the SD card on TLORA — kept deliberately, to not force a field rebuild |
| 490           | 16 MB partition table for N16R8 boards — declined, "same layout for all nodes"                 |
| 82            | Mesh-cloud QUERY — declined on air-time grounds                                                |
| 434           | Passwords in cleartext in `--info` — intentional                                               |
| 587           | `DO_DEBUG=1` gating of some debug output — intentional                                         |
| 249           | INA226 suppressing other sensor values in the POS frame — intentional (aprs.fi 4-value limit)  |
| 851           | Serial (UART) LoRa modules — will not be supported                                             |
| 636           | Portduino / Raspberry Pi as a board variant — deferred to MeshCom 5.0, label applied           |
| 215, 216, 214 | Our own three from 2025-03-14 — declined as not relevant                                       |
| 741           | Our own max-hop report — closed by us, behaviour is intended                                   |

---

## 7. Backlog entries

**Added to `docs/BACKLOG.md` §3.8f on 2026-08-29** as `TM-30` … `TM-33`. (The `TM-25` … `TM-29`
proposed in the first draft of this document collided with items committed to the branch while this
harvest was being read — `bc4e6502`. Renumbered.)

| ID    | Board(s)            | Type | Sev.   | From            | Item                                                                                                                                               |
| ----- | ------------------- | ---- | ------ | --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| TM-30 | T-Deck              | BUG  | High   | #1083           | UI degrades to tens of seconds after ~10 min while web/OTA stay responsive. Bisect `4.35p.07.26` → `4.35p.08.06` on DK5EN-14 under `[INSTR-LOOP]`. |
| TM-31 | all ESP32 (gateway) | BUG  | High   | #568            | UDP→LoRa gateway drops the packet arriving on the shortest inter-arrival gap in a burst. Evidence recovered and analysed — see §4.2.               |
| TM-32 | all                 | BUG  | Medium | #661, #57       | Field-plausibility validation of loaded settings. Extends the `N-12` integrity check, which does not cover in-range-ness.                          |
| TM-33 | T-Deck              | TEST | Medium | #64, #690, #267 | Regression tests for three T-Deck reports closed without a fix. All three verified **still present** on this branch — see §4.4/§4.5.               |

**Not taken, by decision:** #504 and #154 (silent LoRa stop) carry no reproduction evidence — #504 is
"sometimes, after weeks", no log; #154 has zero comments and no log. Accepted as-is; if the stability
PR did fix them, nothing in either thread would tell us. #962/#926 (brown-out restart, low-battery
thresholds) and the pin-conflict check are recorded here but not filed.

---

## 8. Method

```bash
gh issue list -R icssw-org/MeshCom-Firmware --state all --limit 1000 \
  --json number,title,body,state,stateReason,labels,author,createdAt,closedAt,updatedAt,url,comments
```

854 KB of JSON, split into a user-reported digest (154 issues, full threads) and a `karamo` digest
(201 issues, condensed), both read end to end. Closure quality was classified per issue from the last
three comments and then corrected by reading — the regex pass over-reported "fixed" on threads where
the maintainer's last word was a question, and under-reported it where the reporter confirmed in
their own words.
