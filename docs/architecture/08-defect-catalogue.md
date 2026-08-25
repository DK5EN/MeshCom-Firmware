# 08 — Defect Catalogue & Remediation Plan

> **What is actually broken, in what order do we fix it, and how does each fix become
> one commit and one upstream PR?**

Review date 2026-07-31, branch `v4.35p_prio` at `1ba101f4`. Method: 8 independent finder
angles → adversarial verification → orchestrator re-verification of every
decision-relevant claim against the real source, the real toolchain, and the real build.

**Verification legend**

| Mark          | Meaning                                                                                 |
| ------------- | --------------------------------------------------------------------------------------- |
| **VERIFIED**  | Orchestrator independently re-derived it (command shown or cited) — not finder-reported |
| **CONFIRMED** | Finder cited file:line and mechanism; spot-checked, not fully re-derived                |
| **REFUTED**   | Claimed by a finder or by this concept, disproved. Recorded so it is not re-found       |

---

## 0. Prior art — this is not a fresh start

`docs/code-audit-20260712.md` (2026-07-12) already holds **39 verified findings**
(`SEC-01` … `TEST-39`) against this same branch. The `docs/architecture/` concept
(01–07) was written without reading it and re-derived its structural half while missing
its security and concurrency halves entirely.

**Fix status: essentially all 39 are still open.** No commit references any ID
(`git log --all | grep -iE 'SEC-0|BUG-1|CONC-1|DRY-2|SIMP-2|ALT-3|TEST-3'` → empty). The
June 26–27 fix commits predate the verdict and address the earlier `docs/code-audit-*`
series.

**Correction to the prior verdict:** `DRY-20` is labelled `RESOLVED` but its own body says
both battery implementations are live. The label meant "finder disagreement resolved", not
"defect fixed". Re-derived today: 13 variants define `USE_NEW_BATT`, **17 do not** — the
split has grown since the verdict counted 12/15 of 27. **Status: OPEN.**

This catalogue **adopts the existing IDs**. New findings get `N-nn`.

---

## 1. Corrections to this concept (docs 01–07)

The concept contained errors that would have sent work in the wrong direction. Listed
first because several of its recommendations must be withdrawn before anyone acts on them.

### C-01 — `OnRxDone` does not run in interrupt context — **VERIFIED (nRF52 half CORRECTED 2026-07-31)**

01 and 04 state the RX callback runs "off the radio callback" with everything it touches
shared with the main loop. Wrong on both platforms, differently:

- **ESP32:** the only call is `checkRX()` at `esp32_main.cpp:3778`, itself called from
  `esp32loop()` at `:2217`. It runs in `loopTask`. The only ISR is
  `setFlagReceive`/`setFlagSent` (`:487`, `:503`) — nine lines, four atomics. **This half
  stands.**
- **nRF52: the original statement here — "it runs in the SX126x `LORA` FreeRTOS task at
  priority 2, which preempts `loop()` at priority 1" — is WRONG.** See below.

#### Correction: the nRF52 preemption path is a different task

The `"LORA"` task is created at **priority 1**, the same as `loop_task`, and therefore
preempts nothing:

```c
// SX126x-Arduino board.cpp:44      ← #ifndef cannot see an enum, so this macro wins
#ifndef TASK_PRIO_NORMAL
#define TASK_PRIO_NORMAL 1
#endif
// board.cpp:498
xTaskCreate(_lora_task, "LORA", 4096, NULL, TASK_PRIO_NORMAL, &_loraTaskHandle);

// Adafruit core rtos.h:59          ← what the library meant to use
TASK_PRIO_NORMAL  = 2,
// Adafruit core main.cpp:88
xTaskCreate(loop_task, "loop", LOOP_STACK_SZ, NULL, TASK_PRIO_LOW /* =1 */, &_loopHandle);
```

`FreeRTOSConfig.h` additionally sets `configUSE_TIME_SLICING 0`, so equal-priority tasks do
not even round-robin. This is a latent defect in the vendored library, not in this project's
code.

**The genuine preemptor is the FreeRTOS timer service task**, and it is worse than what the
original claim described:

```
radio.cpp:599  TimerInit(&RxTimeoutTimer, RadioOnRxTimeoutIrq)
  → nrf52832/timer.cpp:41  SoftwareTimer timerTickers[10]      (#ifdef NRF52_SERIES)
  → SoftwareTimer.cpp:39   xTimerCreate(...)
  → FreeRTOS timer service task:  configTIMER_TASK_PRIORITY 2
                                  configTIMER_TASK_STACK_DEPTH 256 words = 1 KB
  → RadioOnRxTimeoutIrq() → RadioBgIrqProcess() → RadioEvents->RxDone(...)  == OnRxDone
```

So on nRF52 the 968-line `OnRxDone` — nesting depth 13, seven `String` allocations per
packet, plus display, BLE and UDP enqueue — can run **on a 1 KB stack at priority 2**,
preempting both `loop()` and the `"LORA"` task (both priority 1). The `"LORA"` task's own
stack is 4096 words = 16 KB.

Full derivation, ownership map and the revised interleavings:
[`09-concurrency-map.md`](09-concurrency-map.md). `N-14` below is revised accordingly.

This mislabel was load-bearing — it is the stated justification for `displayMux`.

### C-02 — the radio-interface recommendation is ~10× oversized and mis-targeted — **VERIFIED**

01 §3, 04 item 7 and 05 Phase 3 rank "extract a radio interface, collapse ~3,200 lines of
duplicated scheduler" as the single highest-leverage change. The 3,200 figure is
`1947 + 1233` — the two loop _sizes_, not a duplication measurement.

Measured overlap between `esp32loop()` and `nrf52loop()`: **~221–268 lines (≈15 %)**, in 9
blocks. **None of the shared blocks is radio code** — they are deferred display text, NTP/RTC
parsing, the position beacon and sensor polling, all already radio-independent and
extractable today with zero RF risk.

The real blocker is not a differing API but a differing **concurrency model** (C-01), plus
CAD being synchronous on one side (`radio.scanChannel()`) and asynchronous on the other
(`Radio.StartCad()` → `OnCadDone`). A 7-method interface does not hide that.

**Withdrawn.** Replaced by: extract the ~221 radio-independent shared lines into
`common_loop.cpp`. Small, safe, real.

### C-03 — the golden-vector corpus does not exist — **VERIFIED**

06 Layer 2 and 07 §1.5 call `tools/meshcom_monitor/*.log` "a golden-vector corpus you
already own" and cost the extractor at 3–5 sessions. This is the claimed foundation of the
entire before/after oracle.

Measured across all 17 logs:

| Metric                                   | Value   |
| ---------------------------------------- | ------- |
| Hex dumps (`[MC-DBG] CRC_PAYLOAD[255]:`) | 1,821   |
| `MH-LoRa:` decode lines                  | 25,645  |
| Files containing **any** hex dump        | 3 of 17 |
| **Usable (hex, decode) pairs**           | **0**   |

The reason is structural, not a parsing detail. `esp32_main.cpp:3811-3821` emits
`CRC_PAYLOAD` **only inside the `RADIOLIB_ERR_CRC_MISMATCH` branch**, which returns before
`OnRxDone()` (the sole call, in the sibling `else` at `:3778`) and therefore before
`decodeAPRS()`. **A frame can never be both dumped and decoded.** Every dump is a
CRC-failed frame, and is 255 bytes of `checkRX`'s uninitialised stack — the tails visibly
contain ASCII from earlier `printf` calls. True frame lengths are recorded nowhere, so
06's instruction that "the extractor must honour the length field" is not implementable.

06's own worked example is a mismatched pair: the hex is from `…172422.log:363` at
17:26:21.918 (CRC-failed, RSSI −119, visibly bit-damaged); the `MH-LoRa:` block beside it
is from 17:24:32.263 — **109 seconds and a different frame away**.

**And the oracle would be circular even with correct pairs.** `MH-LoRa:` has one emit site
(`lora_functions.cpp:496` → `printBuffer_aprs`), which pretty-prints the struct
`decodeAPRS()` just filled. Asserting against it proves `decode_new(x) == decode_today(x)`
— a regression fence, never a correctness oracle. The corpus is also self-selecting
(`decodeAPRS` returns `0x00` on FCS mismatch) and lossy (`lat`/`lon`/`alt` are not in the
`MH-LoRa:` format string at all).

**Withdrawn.** See §4 for what replaces it. This is the most consequential correction in
the review: the zero-tolerance before/after requirement had no foundation.

### C-04 — the Arduino 2.0.17 headline is false for four boards — **VERIFIED**

03 states every `espressif32` release pins `~3.20017.0`. Re-derived from the platform
manifests:

| Platform version | `framework-arduinoespressif32` | Arduino core |
| ---------------- | ------------------------------ | ------------ |
| 6.5.0            | `~3.20014.0`                   | **2.0.14**   |
| 6.6.0            | `~3.20014.0`                   | **2.0.14**   |
| 6.13.0           | `~3.20017.0`                   | 2.0.17       |

So `t_deck` and `t_deck_plus` (`@ 6.6.0`) and `t_deck_pro` and `t5_epaper` (`@ 6.5.0`)
already run **Arduino 2.0.14 / IDF 4.4.5** — a different core from the other 26 boards.
The doc's headline denies exactly this. "Bumping the platform buys nothing" is wrong for
those four: it is a real core update, and it belongs in the sequence.

### C-05 — 02's finding B-01 has the wrong mechanism and a board-breaking fix — **VERIFIED**

02 claims `[nrf52]`, declared in three variant files, "collides and the effective content
depends on glob order". ConfigParser merges duplicate sections option-by-option; there is
no race. `pio project config --json-output` shows `heltec_t114` and `t_echo`
**deterministically** receive from the `wiscore_rak4631` copy:

```
-D BOARD_RAK4630="RAK4630"        # used at 66 #if/#elif sites
-Isrc/nrf52
build_src_filter: … +<../variants/wiscore_rak4631/*>
```

02's proposed cleanup — move `[nrf52]` to root, push the wiscore-specific filter line into
`[env:wiscore_rak4631]` — would silently strip all three from two shipping boards.
**Withdrawn as written.** The section duplication is still worth removing, but only
together with explicit per-board restatement of these three options.

### C-06 — remaining concept corrections — **CONFIRMED**

| ID   | Doc         | Claim                                                               | Reality                                                                                                                                                                                                                                                            |
| ---- | ----------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| C-06 | 07 §1.1     | `-D LORA_ISR_DEBUG` unlocks 5 ISR traces on any board               | All 5 sites sit inside `#if defined BOARD_RAK4630`. **No-op on the Heltec V3 bench.**                                                                                                                                                                              |
| C-07 | 04          | `mheard` ↔ `ui_deckpro` layering violation, ~200 lines              | **Inverted**: it is LVGL table code (`showMHeardTDECK`) living in `mheard_functions.cpp`, 54 lines. Remediation item 4 moved code the wrong way.                                                                                                                   |
| C-08 | 04          | "218 commands"                                                      | 217 arms / 227 distinct verbs. The `--setinfo off` before `--setinfo` example is invented — no bare `--setinfo` exists.                                                                                                                                            |
| C-09 | 04          | `commandAction` is one flat if/else chain, refactor is "mechanical" | 7 chain segments (breaks at `:1478, 2227, 2749, 2981, 3205, 3532`) feeding an 11-flag shared continuation, ~190 set-sites. `--info` matches at `:719`, executes at `:4694`. `commandCheck` is a **prefix** match, so the doc's binary-search alternative is wrong. |
| C-10 | 04          | CC counts `&&`/`                                                    |                                                                                                                                                                                                                                                                    | `   | `arch_metrics.py`'s `\b…\b` wrapper cannot match them. The stated formula is wrong; rankings stand. |
| C-11 | 03          | quotes the "tight boards" ring config                               | That block is `#if ENABLE_TBEAM` — **defined nowhere**. Dead code quoted as live.                                                                                                                                                                                  |
| C-12 | 03          | step 0b "RAM baseline across all 32 envs"                           | `tools/ram_snapshot.py` hardcodes 7 targets. Not executable as written.                                                                                                                                                                                            |
| C-13 | 01/06/07    | concept covers concurrency                                          | "dual-core", "core 0", "core 1", "pinnedToCore" appear **nowhere** in 01–07. No core model at all.                                                                                                                                                                 |
| C-14 | 06 §Layer 1 | native extraction costed at 5–8 sessions                            | Measured closure: `aprs_functions.cpp` needs 7 globals + 3 stubs; `via_functions.cpp` 9 + 5. **1–2 sessions.** Pessimism was deferring the cheapest layer.                                                                                                         |
| C-15 | 07 §5       | `pio test` on device                                                | Blocked twice: `src/main.cpp` has no `PIO_UNIT_TESTING` guard, and `upload_protocol = custom` hardcodes `${build_dir}/${env}/firmware.bin` while `pio test` emits to `<env>/<test>/firmware.bin`. Flash headroom is fine (1.49 of 3.40 MB).                        |
| C-16 | 05          | large contributions are "structurally unmergeable" upstream         | **24 PRs from this author are merged upstream, 0 open**, from `+1 −0` to `+3984 −1516`. Assumption, not measurement. Withdrawn.                                                                                                                                    |
| C-17 | 07 §1.4     | recommends `--debug csv` as the machine-readable mode               | 15,783 of 25,645 captured lines carry payload semicolons that pass through unescaped. CSV mode is not parseable on real traffic.                                                                                                                                   |

---

## 2. Live defects — new findings

Severity reflects reachability from untrusted input. **RF** = any node in radio range,
unauthenticated, unattributable.

### N-01 — `{MCP}` remote command: password check is a character-set membership test — **VERIFIED** — Critical, RF

> **STATUS 2026-08-18 — AKZEPTIERT / WONTFIX (Maintainer-Entscheidung).**
> Als Sicherheitsrisiko angenommen und bewusst nicht gefixt.
> Der Befund bleibt technisch gueltig und ist hier unveraendert dokumentiert;
> er wird auf `v4.35p` nicht behoben. Erneut aufgreifen nur, wenn die Begruendung
> unten entfaellt.

`src/loop_functions.cpp:2126-2140` (was `:2057` pre-rebase)

```c
for(int ip=0;ip<5;ip++) {
    if(cpasswd[ip] != 0x00 && bpass) {
        bool bp=false;
        for(int ic=0;ic<15;ic++)
            if(meshcom_settings.node_passwd[ic] == cpasswd[ip])
                bp=true;                 // membership, not equality
        bpass = bp;
    }
}
```

Two independent bypasses:

1. **Space padding.** `command_functions.cpp:3007` stores the password as `"%-14.14s"` —
   left-justified, space-padded to 14. Any password shorter than 14 characters therefore
   contains `0x20`, and five spaces satisfy the test. Order, position and repetition are
   all irrelevant.
2. **Zero bytes.** `bpass` is initialised `true` and the inner check runs only for
   `cpasswd[ip] != 0x00`. `cset` is zeroed and the password field sits at offset 12, so a
   payload short enough leaves all five bytes zero — the loop never executes and `bpass`
   stays `true`.

The second gate compares three characters against decimal digits of
`aprsmsg.msg_id & 0x3FF` — the attacker's own frame, freely chosen. The handler then
reaches `commandAction("--setout …")` for GPIO control.

**Relation to SEC-01:** SEC-01 found only the empty-password path. Its proposed fix
(`if(!bpass || cpasswd[0]==0x00) return;`) **does not close the membership test.**

**Fix:** constant-time `memcmp` against the stored password with an explicit length, after
rejecting an unset password. Regression test: five spaces must fail against a 6-character
password.

### N-02 — `{SET}` mutates mesh routing with no authentication — **VERIFIED** — Critical, RF

> **STATUS 2026-08-18 — AKZEPTIERT / WONTFIX (Maintainer-Entscheidung).**
> Als Sicherheitsrisiko angenommen und bewusst nicht gefixt.
> Der Befund bleibt technisch gueltig und ist hier unveraendert dokumentiert;
> er wird auf `v4.35p` nicht behoben. Erneut aufgreifen nur, wenn die Begruendung
> unten entfaellt.

`src/loop_functions.cpp:2190-2196` (was `:2121` pre-rebase)

```c
if(aprsmsg.msg_payload.startsWith("{SET}") > 0) {
    char cset[30];
    snprintf(cset, sizeof(cset), "%s", aprsmsg.msg_payload.c_str());
    sscanf(cset+5, "%d;%d;", &meshcom_settings.max_hop_text, &meshcom_settings.max_hop_pos);
    return;
}
```

No password gate, no range check, `%d` into signed fields, written straight into persisted
settings. One broadcast `{SET}0;0;` sets `max_hop == 0` on every node in radio range: each
keeps transmitting, nobody relays it, and every affected node is silently cut out of the
mesh. No log line, no visible symptom. **Not in any prior document.**

**Fix:** same auth gate as `{MCP}`, plus range clamp to the documented hop limits.

### N-03 — CONF zero-fill writes up to 251 bytes past a stack buffer — **VERIFIED** — Critical, UDP

`src/nrf52/nrf_eth.cpp:517`. Found independently by two finders.

```c
uint8_t config_buf[UDP_CONF_BUFF_SIZE] = {0};        // 255, already zeroed
…
for (int i = 0; i < UDP_CONF_BUFF_SIZE; i++)
    config_buf[packetSize - UDP_MSG_INDICATOR_LEN + i] = 0x00;
```

The counter starts at 0 but the index is biased by the packet length. `packetSize == 255`
writes `config_buf[251 … 505]`. Unauthenticated UDP to the CONF port on nRF52 with
Ethernet. **The loop is also redundant** — the buffer is already `= {0}`.

**Fix:** delete the loop. The ESP32 counterpart is clean — exactly the divergence `DRY-21`
predicted.

### N-04 — `memcpy` with a length near `SIZE_MAX` from one RF frame — **VERIFIED** — Critical, RF

Chain of three sites:

1. `loop_functions.cpp:527` — `if (len > UDP_TX_BUF_SIZE) len = UDP_TX_BUF_SIZE-4;` lets
   `len ∈ {252,253,254,255}` through unchanged.
2. `loop_functions.cpp:548` — `BLEtoPhoneBuff[toPhoneWrite][0] = len + 4;` into one byte →
   wraps to 0,1,2,3. (This is `BUG-08`.)
3. `web_functions.cpp:1279,1296` — `uint8_t blelen = …[0];` then
   `memcpy(toPhoneBuff, …, blelen - 4);`. Promotion to `int` gives −4…−1; conversion to
   `size_t` gives ≈`SIZE_MAX`.

**Trigger:** a 252–255 byte LoRa text frame with a valid FCS, then any load of the web
messages page. Immediate hard fault.

**Fix:** clamp in `addBLEOutBuffer` (`len > UDP_TX_BUF_SIZE-4`), and bounds-check `blelen`
before the subtraction.

> **STATUS 2026-08-18 — BEHOBEN.** `addBLEOutBuffer()` klemmt jetzt zweigabhaengig
> (`UDP_TX_BUF_SIZE-4` im Zeitstempel-Zweig, volle 255 im `'D'`/JSON-Zweig, der kein `+4`
> anhaengt), und der Konsument in `web_functions.cpp` prueft `blelen` vor jeder Subtraktion.
> Zusaetzlich klemmt `addBLEComToOutBuffer()` seine Laenge jetzt tatsaechlich auf 245 --
> bisher wurde der Fehler nur geloggt und danach unveraendert kopiert.
>
> **Restbefund — BEHOBEN 2026-08-20** (Commit `6268667a`). `src/phone_commands.cpp` las
> dasselbe Laengenbyte in `sendToPhone()` und `sendComToPhone()` und rechnete `blelen-1`
> ohne eigene Pruefung; `blelen==0` haette zu `uint8_t`-Unterlauf (255) und einem
> Ueberlesen des Ring-Slots gefuehrt. Beide Funktionen brechen jetzt direkt nach dem Lesen
> von `blelen` bei `blelen==0` ab (Read-Pointer wird trotzdem weitergeschaltet). Im selben
> Zug (Commit `ed9116f6`) wurde `sendToPhone()` zusaetzlich auf einen Snapshot-Puffer
> umgestellt, siehe `CONC-18`.

### N-05 — heap over-read is rebroadcast over the air — **VERIFIED** — High, RF

> **STATUS 2026-08-18 — BEHOBEN.** Beide `memcpy` in `updateHeyPath()` leiten ihre Laenge
> jetzt aus der Quelle ab (`ipc` bzw. neu `icallsize`), nicht mehr aus `sizeof(dest)`.
> `ipc` wird zusaetzlich gegen negative Werte geklemmt.

`src/mheard_functions.cpp:526` and `:532` (unchanged by the rebase)

```c
int ipc = mheardLine.mh_sourcepath.length() - ips;
if(ipc > 37) ipc = 37;                                    // computed correctly…
…
memcpy(mheardPathBuffer1[ipos],
       mheardLine.mh_sourcepath.substring(ips).c_str(),
       sizeof(mheardPathBuffer1[ipos]));                  // …and never used. 50 bytes.
```

Length is the **destination** size (50). The source is a temporary `String` from an
RF-derived path that may be a few bytes. Up to ~44 bytes of adjacent heap are copied in,
then displayed, JSON-exported to the phone, and used to build **outgoing HEY paths**.

**Fix:** use `ipc`.

### N-06 — unbounded array index from a web parameter — **VERIFIED** — Critical, LAN

> **STATUS 2026-08-18 — BEHOBEN.** Alle drei Stellen (`mcpio`, `mcpout`, `mcpname`) pruefen
> jetzt Bank `A`/`B` und Ziffer `0`-`7`, bevor der Index gebildet wird; ungueltige Eingabe
> liefert `WS_RETURNCODE_FAIL` ohne Schreibzugriff.

`src/web_functions/web_setup.cpp:500`, `:527` and `:550` — **three sites**, not one (was cited as `:551` pre-rebase)

```c
uint8_t t_io = (uint8_t)port.charAt(1) - 48;
if(port.charAt(0)=='B') t_io+=8;
snprintf(meshcom_settings.node_mcp17t[t_io], sizeof(…), "%s", …);
```

Only `port.length() == 2` is checked. `charAt(1) == 0xFF` gives `t_io = 215` against
`node_mcp17t[16][16]` → a 16-byte write ~3.4 kB past the array, inside `meshcom_settings`,
followed by `save_settings()`.

**Fix:** validate `t_io < 16` and reject otherwise.

### N-07 — BLE command channel is unauthenticated and ungated — **VERIFIED** — Critical, BLE range

> **STATUS 2026-08-18 — AKZEPTIERT / WONTFIX (Maintainer-Entscheidung).**
> Der wirksame Fix ist BLE-Bonding/Verschluesselung (`setSecurityAuth`, `WRITE_ENC`/`READ_ENC`, `setSecurityPasskey(bt_code)` -- alle drei sind im Code bewusst deaktiviert). Einschalten trennt jede bestehende Phone-App, bis der Nutzer neu koppelt. Diese Entscheidung gehoert zum Upstream-Projekt, nicht in diesen Branch.
> Der Befund bleibt technisch gueltig und ist hier unveraendert dokumentiert;
> er wird auf `v4.35p` nicht behoben. Erneut aufgreifen nur, wenn die Begruendung
> unten entfaellt.

`esp32_main.cpp:1596` `setSecurityAuth(false,false,false)`; the `WRITE_ENC` / `READ_ENC`
properties are commented out a few lines below; the `if(hasMsgFromPhone)` dispatch calls
`commandAction` with **no `isPhoneReady` gate** — that check begins only in the following
block. (Line numbers shifted ~8 by the rebase; re-derive before fixing.) Any BLE device in
range reaches the full command surface including `--cleanflash`, `--setpwd`, `--ota-update`.

A prior audit accepted this as "only a buggy app could do that". The code does not
support that reading.

### N-08 — `millis()` rollover: the safe idiom is not universal — **VERIFIED** — Medium

The A1 "millis() wraparound" fixes in four June commits converted ~70 sites to the safe
`(uint32_t)(millis() - t) >= N` form. Still remaining:

- **33** assignments of the form `X = millis() + N`
- **11** comparisons of the form `millis() > X`

including **both** platforms' `rebootAuto` (`esp32_main.cpp:3173`, `nrf52_main.cpp:2019`)
and `DisplayOffWait` (`:3153`, `:2000`). After 49.7 days of uptime the scheduled reboot
either never fires or fires immediately. Mesh nodes run for months.

> **STATUS 2026-08-18 — BEHOBEN.** 25 direkte Vergleiche (21 aus der urspruenglichen
> Fundstellenliste plus 4 in umgekehrter Schreibweise `(X) < millis()`, die derselbe
> Grep-Lauf nicht erfasst hatte) mechanisch auf `(int32_t)(millis() - X) <op> 0`
> umgeformt -- dieselbe Subtraktionstechnik, die im Code bereits an ~70 Stellen fuer
> Intervallvergleiche etabliert ist. Die Zuweisungen (`X = millis() + N`) waren bereits
> sicher und blieben unveraendert. Regressionstest `test/test_millis_rollover/`
> bildet den Ueberlaufmechanismus nach; die Aufrufstellen selbst sind wegen
> Hardware-Abhaengigkeiten nicht nativ testbar. Vier Boards sauber gebaut
> (`heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `t_deck`, `t_deck_pro`), RAM
> unveraendert, Flash +16..+64 Byte je Board.

### N-09 — 11 × `while (true);` hard hang — **VERIFIED** — High

`src/t-deck-pro/peri_lora.cpp:48–114`. The 2026-06-26 audit mandated a fix and explicitly
pre-rejected the T-Deck-Pro exemption in writing ("_trotz T-Deck-Pro: geteilter
Code-Pfad_"). `code-audit-fixes-20260627.md:70` then used that exemption and marked the row
✅ done. `t_deck_pro` is in `default_envs` and ships as a CI release artifact.

> **STATUS 2026-08-18 — KORRIGIERT, KEIN LIVE-BEFUND.** Re-verifiziert gegen den aktuellen
> Baum: der gesamte Koerper von `lora_init()` -- einschliesslich aller elf `while (true);` --
> steckt in einem einzigen `/* ... */`-Blockkommentar, der seit dem einzigen Commit dieser
> Datei (`462da95f`, 2025-08-02) existiert. Die Funktion tut nur `return true;` und
> `peri_init_st[E_PERI_LORA]` (`tdeck_pro.cpp:509`) wird nirgends gelesen. Die elf Haenger
> sind also seit ueber einem Jahr toter Code, nicht die vom 2026-06-26-Audit angenommene
> aktive Gefahr. **Bewusst kein Code-Fix**: eine Aenderung an totem, auskommentiertem Code
> haette keine Laufzeitwirkung; das Reaktivieren des ganzen `lora_init()`-Pfads waere eine
> eigene, deutlich groessere Entscheidung (T-Deck-Pro-LoRa-Init ueber RadioLib scharfschalten)
> und liegt ausserhalb dieses Befunds. Severity von High auf **keine** korrigiert. Erneut
> aufgreifen, falls `lora_init()` jemals aus dem Kommentar geholt wird.

### N-10 — board identity macros are arithmetic on product names — **VERIFIED** — Medium

> **STATUS 2026-08-18 — BEHOBEN.** **Zwoelf** Guards (nicht zehn) nutzen jetzt
> `defined(BOARD_E22_S3)`. Die zwei zusaetzlichen sitzen in `src/aht20.cpp:45,70` und waren
> in jeder bisherigen Grep-Verifikation unsichtbar: die Datei ist **ISO-8859-kodiert**, BSD-`grep`
> behandelt sie ohne `-a` als Binaerdatei und ueberspringt sie stillschweigend. Aufgedeckt hat
> sie erst ein `-Wundef`-Testlauf. Konsequenz fuer den Rest der Kampagne: **jede Grep-Prueffung
> ueber `src/` braucht `-a`**, sonst ist sie fuer `aht20.cpp` wertlos.
>
> Verhaltensgleichheit gegengeprueft: `pio project metadata` liefert
> `BOARD_E22_S3=esp32-s3-devkitc-1-n16r8` — die Anfuehrungszeichen werden also tatsaechlich
> verschluckt, der alte Ausdruck war `0-0-0-1-0 = -1` und damit **wahr**. Der Fix ist fuer alle
> Boards verhaltensneutral (gegen die anderslautende Behauptung eines Sub-Agenten geprueft).
>
> **`-Wundef` wurde bewusst NICHT im Build aktiviert.** Gemessen an
> `heltec_wifi_lora_32_V3` + `E22_1262_S3`: 10.317 `-Wundef`-Treffer, davon **2 aus `src/`**,
> der Rest aus ESP-IDF-/Arduino-Headern. `build_src_flags` hilft nicht, weil die Warnungen aus
> den eingebundenen Headern stammen, nicht aus unseren Uebersetzungseinheiten. Das haette die
> drei echten Bestandswarnungen zugeschuettet und den Weg zu `-Werror` (Wave 0.2) verbaut.
>
> Als Regressionsschutz stattdessen ein CI-Job `macro-guards` in
> `.github/workflows/ci-build.yml`, der genau die fehlerhafte Schreibweise sucht (mit `-a`).
> Beidseitig selbstgetestet: findet auf dem aktuellen Baum nichts, schlaegt bei der alten Form an.
>
> `bmx280.cpp:131,143` bleiben unveraendert — Folgeaufgabe.

`platformio.ini` writes `-D BOARD_E22_S3="esp32-s3-devkitc-1-n16r8"`; PlatformIO's ini
parser **consumes the quotes**, so the compiler receives an identifier sequence
(`pio run -v` confirms). At 12 sites in 5 files,
`#if defined(BOARD_TBEAM_V3) || (BOARD_E22_S3)` therefore evaluates
`0 - 0 - 0 - 1 - 0 == -1` → true. The guard works **by accident**:

```
$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3=esp32-s3-devkitc-1-n16r8 pp.c  → guard = 1
$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3=esp32-s3-devkitc-0-n16r8 pp.c  → guard = 0
```

One digit in a marketing string removes the I²C bus-reset workaround, with no diagnostic.
**28 `BOARD_*` macros** are defined this way. `-Wundef` catches it and is enabled nowhere.

Sites: `aht20.cpp:45,70`, `bmp390.cpp:54,84`, `bmx280.cpp:169,213`,
`rtc_functions.cpp:28,71,83,99`, `sht21.cpp:41,68`. The prior verdict (`DRY-25`) listed 9
sites in 4 files and **missed `rtc_functions.cpp` entirely**; two sites it did list
(`bmx280.cpp:131,143`) have since been changed to plain `#ifdef BOARD_TBEAM_V3`, so E22_S3
now silently lacks the workaround there — a regression introduced after the verdict.

### N-11 — GPL-3.0 libraries statically linked into an MIT-licensed firmware — **CONFIRMED** — legal

`lib/GxEPD2` and `lib/ESP32-audioI2S` are GPL-3.0; `lib/epdiy` and `lib/TinyGSM` are
LGPL-3.0; `lib/es7210` and `lib/Timeout` carry no licence. The root `LICENSE` is MIT.
Also: an external contributor patched `lib/TinyGSM/` in place, invisible to `lib_deps`.

Not a code defect, but it is the kind of thing that surfaces at the worst moment. Needs a
decision, not a fix.

> **STATUS 2026-08-18 — AKZEPTIERT (Maintainer-Entscheidung).**
> Das Lizenzrisiko wird angenommen, kein Aenderungsauftrag daraus abgeleitet. Der Befund
> bleibt technisch gueltig und ist hier unveraendert dokumentiert.

### N-12 — `FLASH_VERSION` neither migrates nor resets — **Teil-FIX (`14e826b8`), Struct-Vereinheitlichung bleibt offen** — High

> **STATUS 2026-08-22: die zwei nRF52-lokalen Defekte GEFIXT (`14e826b8`,
> Hardware-verifiziert).** (a) `flash_reset()` invalidiert jetzt
> `init_flash_done` — `--cleanflash`/Version-Mismatch liefert echte Defaults
> (Bench: Call `XX0XXX-00` nach Reset, vorher kamen die alten Werte zurueck).
> (b) Groessen-Check: Datei muss exakt `sizeof(s_meshcom_settings)` sein,
> sonst In-Place-Reset auf Defaults (ersetzt die alte Format+Reboot-Schleife);
> Bestandsdateien bestehen den Check (Boot mit Bestandsdaten geprueft).
> Blinder Fleck dokumentiert: sizeof-neutrale Layout-Tauschungen — Layout-
> Aenderungen muessen FLASH_VERSION erhoehen. **Weiter offen (unveraendert):**
> die zwei inkompatiblen Struct-Layouts ESP32/nRF52 — Vereinheitlichung samt
> Migration bleibt das eigene Upstream-Vorhaben.

The version check runs _after_ `init_flash()` and is not followed by a re-read, so the old
RAM copy is written straight back — while the log prints `FLASH cleared new version`.
There are two incompatible `meshcom_settings` layouts (2008 B ESP32 / 1968 B nRF52); nRF52
raw-`memcpy`s its struct with two marker bytes as the only integrity check, so inserting a
field mid-struct corrupts every nRF52 node in the field. `--cleanflash` is a no-op on
nRF52.

This is the highest-risk item for any fleet-wide settings change.

> **STATUS 2026-08-18 — RE-VERIFIZIERT, BEWUSST KEIN CODE-FIX.** Mechanismus gegen den
> aktuellen Baum bestaetigt: `src/nrf52/nrf52_flash.cpp:38-41` hat einen
> `init_flash_done`-Guard, der den zweiten `init_flash()`-Aufruf nach
> `flash_reset()` (ausgeloest durch Versions-Mismatch oder `--cleanflash`) sofort
> zurueckkehren laesst, ohne neu von Flash zu lesen. `save_settings()`
> (nrf52_main.cpp:533) schreibt dadurch die **unveraenderte RAM-Kopie** zurueck --
> nur `node_fversion` wird aktualisiert -- waehrend das Log "FLASH cleared new
> version" meldet. Auf ESP32 hat `init_flash()` (esp32_flash.cpp:14) keinen
> solchen Guard und liest nach `clear_flash()` tatsaechlich frische Defaults.
>
> Struct-Layouts erneut verglichen: `esp32_flash.h:85-88` vs.
> `nrf52/WisBlock-API.h:250-253` haben `node_mcp17io`/`node_mcp17t`/`node_mcp17out`/
> `node_mcp17in` in **unterschiedlicher Reihenfolge**, nRF52 hat zusaetzliche Felder
> (`send_repeat_time`, `auto_join`) ohne ESP32-Aequivalent, und die Datumsfelder
> sitzen an unterschiedlichen Stellen relativ zu `node_opwd`/`node_ossid`. Die
> nRF52-Integritaetspruefung besteht aus genau zwei Marker-Bytes
> (`valid_mark_1=0xAA`, `valid_mark_2=0x55`/`0x57`) -- eine Layout-Verschiebung
> waere fuer diese Pruefung unsichtbar.
>
> **Bewusst kein Fix in dieser Session.** Der isolierte Bug (der
> `init_flash_done`-Guard) waere als Einzeiler entfernbar, aber ohne gespeichertes
> Task-Handle fuer Re-Entranz-Pruefung von `InternalFS.begin()` und ohne
> Struct-Vereinheitlichung zwischen den Plattformen waere ein "kleiner" Fix hier
> genau die Art Aenderung, die das Projekt unter Zero-Tolerance-Regel ausschliesst
> -- nicht ohne Hardware-Verifikation auf beiden Plattformen testbar, und das
> eigentliche Risiko (Layout-Mismatch, schwache Integritaetspruefung) bliebe
> unangetastet. Bleibt wie zuvor: **Trigger fuer erneutes Aufgreifen ist eine
> dedizierte Struct-Vereinheitlichung, nicht ein Gelegenheits-Fix.**

### N-13 — over-synchronisation: atomics and locks where there is no concurrency — **VERIFIED**

Direct answer to the project goal _"atomics exactly where there is genuine concurrent
access, and nowhere else"_. Because of C-01, the ESP32 LoRa path is single-context, so
**14 objects can drop synchronisation on ESP32**. Two are outright dead:

- **`scanFlag`** (`esp32_main.cpp:473`) — zero readers, zero writers anywhere in the tree.
  `code-audit-fixes-20260627.md:29` records "made atomic" as a completed fix on a variable
  nobody uses.
- **`ch_util_rx_start`** — never written on ESP32; the source says so at `:3775`.

And `displayMux` is worse than redundant: `queueDisplayText` (`lora_functions.cpp:127-141`)
copies an `aprsMessage` containing **seven Arduino `String`s** — seven heap allocations —
inside `portENTER_CRITICAL(&displayMux)`, i.e. with interrupts disabled and a cross-core
spinlock held. On nRF52 the same code runs under `taskENTER_CRITICAL()`.

> **STATUS 2026-08-18 — BEHOBEN (drei von 14 Kandidaten, Umfang bewusst begrenzt).**
> `scanFlag` geloescht (bestaetigt: null Leser, null Schreiber im gesamten Baum).
> `displayMux`-Spinlock auf ESP32 an allen drei Stellen entfernt — Schreiber
> (`queueDisplayText`/`queueDisplayPosition`, `lora_functions.cpp`) und Leser
> (`flushDeferredDisplayUpdates`, `esp32_main.cpp`, bislang nicht als eigene
> Fundstelle erfasst — schuetzte exakt dieselben Felder wie die zwei Schreiber und
> war daher ebenso ueberfluessig); nRF52s `taskENTER_CRITICAL()` unveraendert.
> `ch_util_rx_start` auf einen plattformbedingten Typ umgestellt: einfacher Wrapper
> ohne Atomics auf ESP32, `std::atomic` unveraendert auf nRF52 — der Schreibpfad
> (`OnHeaderDetect`) wird auf ESP32 nie als Radio-Callback registriert.
>
> Vier Boards sauber gebaut (`heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `t_deck`,
> `t_deck_pro`); ESP32-Boards RAM -16 Byte / Flash -108..-120 Byte, `wiscore_rak4631`
> unveraendert (nRF52-Pfad nicht beruehrt).
>
> **STATUS 2026-08-18 (Fortsetzung, Commit `d66683d3`) — vier weitere Kandidaten
> behoben.** `is_receiving`, `ch_util_tx_start`, `ch_util_rx_accum`, `ch_util_tx_accum`
> re-verifiziert und auf denselben plattformbedingten Wrapper wie `ch_util_rx_start`
> umgestellt (einfacher Typ ohne Atomics auf ESP32, `std::atomic` unveraendert auf
> nRF52) — alle vier werden auf ESP32 ausschliesslich ueber die synchrone
> `esp32loop() -> checkRX() -> OnRxDone()`/`OnTxDone()`-Kette angefasst. Jede
> Wrapper-Methode wurde gegen die tatsaechlichen Aufrufstellen in `lora_functions.cpp`
> und `esp32_main.cpp` geprueft, bevor sie ergaenzt wurde.
>
> Die beiden verbleibenden Namen aus der urspruenglichen Liste sind **kein
> Restbefund mehr**: `transmissionState` ist ein einfaches `volatile int`, nie atomar
> oder gesperrt — kein Fix noetig. Die `pendingDisplay*`-Felder waren bereits durch
> das Entfernen von `displayMux` in der ersten Runde geloest, keine eigene
> Synchronisation mehr vorhanden.
>
> **Damit ist die urspruengliche 14er-Liste vollstaendig abgearbeitet** (3 geloescht/
> umgestellt in Runde 1, 4 umgestellt in Runde 2, 2 als kein echter Befund bestaetigt).
> `iWrite`/`iRead`/`loraWrite` (Ringpuffer-Indizes) wurden bei der Re-Verifikation
> zusaetzlich als moegliche Kandidaten derselben Kategorie gefunden, waren aber nicht
> Teil der urspruenglichen Liste und sind bewusst nicht angefasst — eigene Pruefung
> noetig.
>
> heltec_wifi_lora_32_V3 und wiscore_rak4631 sauber gebaut; ESP32-Flash weitere
> -168 Byte, nRF52 unveraendert.

### N-14 — nRF52 TX ring is multi-writer with no mutual exclusion — **FIXED (`efb2381b`, auf Hardware verifiziert)** — High

> **STATUS 2026-08-22: GEFIXT.** Reserve-und-Kopiere-Umbau: `addTxRingEntry()`
> uebernimmt den gesamten Enqueue (Slot-Wahl, Payload-Kopie, Prio/Overflow,
> iWrite/iRead) unter `taskENTER_CRITICAL` (nur nRF52; ESP32 per C-01
> single-context, lock-frei); alle 16 Aufrufstellen schreiben nicht mehr
> selbst in den Ring, Debug-Ausgaben laufen nach dem Lock. Bench-verifiziert
> fuer alle drei Kontextklassen: Loop-Enqueue (DM), Timer-Task-ACK,
> Timer-Task-Relay — Slots, Prios und msg_id-Matches korrekt. Restrisiko
> des Beacon-Nachtrags (READY→DONE-Zweischritt) im Code quantifiziert.

`iWrite` is loaded three times across `ringBuffer[iWrite][0]=…; memcpy(ringBuffer[iWrite]+2,…); addTxRingEntry()`.
Two writers can interleave mid-`memcpy`, both fill the same slot, and a spliced frame goes
on the air. **The C1 "atomic iWrite/iRead" fix does not address this** — atomic indices are
not mutual exclusion.

> **CORRECTED 2026-07-31 — the interleaving mechanism stated here was wrong.** The original
> text said "the `LORA` task preempts `loop_task`". It cannot: both run at priority 1 and
> `configUSE_TIME_SLICING` is 0 (see [C-01](#c-01--onrxdone-does-not-run-in-interrupt-context--verified-nrf52-half-corrected-2026-07-31)).
> The two real interleavings are (a) the **FreeRTOS timer service task at priority 2**,
> which reaches `OnRxDone` via `RadioOnRxTimeoutIrq → RadioBgIrqProcess`, and (b) a yield
> point inside the `"LORA"` task's own enqueue path (`printfdeb` → `Serial.printf` →
> `yield()` when the CDC FIFO is full). The defect and its severity are unchanged; only the
> explanation of _how_ two writers meet is corrected. Current line numbers and the rewritten
> interleaving: [`09-concurrency-map.md`](09-concurrency-map.md).

The correct pattern already exists in this codebase and should be the template: the nRF52
CAD flag protocol at `nrf52_main.cpp:390-394` and `:1332-1338` (atomics plus symmetric
snapshot critical sections on both sides).

### N-15 — a removed guard is justified by a platform-specific assumption — **VERIFIED** — High

`src/phone_commands.cpp:529`

```c
// Spin-wait removed: readPhoneCommand now runs in Main Loop,
// no cross-core conflict with sendToPhone() possible
```

True on ESP32 (`bleQueue` defers). **False on nRF52**, where `api_functions.cpp:254` calls
`readPhoneCommand` directly in the Bluefruit callback task at priority 2. The guard was
removed globally on a platform-specific premise.

> **STATUS 2026-08-20 — RE-VERIFIZIERT, BEREITS GESCHLOSSEN (kein Code-Fix noetig).**
> `api_functions.cpp:254` existiert in dieser Form nicht mehr — die Datei enthaelt keinen
> Aufruf von `readPhoneCommand` oder Bezug auf `bleQueue`. Der zitierte Direktaufruf war der
> Mechanismus vor `CONC-14` (`a441ece6`, 2026-08-18): `bleuart_rx_callback()`
> (`nrf52_ble.cpp:260-281`) enqueued jetzt einen `BleQueueItem` (`xQueueSend(bleQueue, ...)`),
> und `readPhoneCommand()` wird ausschliesslich beim Drainen dieser Queue in `nrf52loop()`
> aufgerufen (`nrf52_main.cpp:1531-1536`) — also im Main Loop, exakt wie der entfernte
> Guard-Kommentar es voraussetzt. `CONC-14` war als BLE-spezifischer Fix eingecheckt, hat
> N-15 aber als Nebenwirkung mitgeschlossen; das war zuvor nicht nachgeprueft (siehe
> `docs/BACKLOG.md` §3.4: "`CONC-15`/`16`/`17`/`18` were **not** re-verified... treat as still
> open" — dieser Satz galt faelschlich auch fuer N-15). Gueltigkeitsbedingung: sobald ein
> zweiter Empfangspfad `readPhoneCommand` wieder inline statt ueber `bleQueue` aufruft
> (z.B. `settings_rx_callback`, das aber ohnehin nicht `readPhoneCommand` nutzt), erneut
> pruefen.

### N-16 — blocking work inside critical sections on nRF52 — **CONFIRMED** — High

`Radio.Send()` inside `taskENTER_CRITICAL()` (`lora_functions.cpp:1685`, `:1726`, `:1787`)
reaches `SX126xWaitOnBusy()` → `delay(1)` → `vTaskDelay()` **with the tick frozen**.

> **STATUS 2026-08-20 — BEHOBEN** (`cc79611b`). Alle drei Fundstellen (`doTX()`, Track-Beacon/
> APRS/ACK-Retransmit) von `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` auf
> `vTaskSuspendAll()`/`xTaskResumeAll()` umgestellt. Nachgewiesen: `RadioOnRxTimeoutIrq` (der
> Callback, vor dem der Guard tatsaechlich schuetzen sollte) laeuft ueber Adafruits
> `SoftwareTimer` (`nrf52832/timer.cpp:62`, wraps `xTimerCreate`/`xTimerStart`) auf dem
> FreeRTOS Timer-Service-Task — echter Task-Kontext, keine ISR. `vTaskSuspendAll()` sperrt
> Task-Scheduling und haelt diesen Task damit fern, laesst Interrupts und den Tick aber
> laufen, sodass `delay(1)` in `SX126xWaitOnBusy()` (`sx126x-board.cpp:138-152`) weiterhin
> zurueckkehren kann. `wiscore_rak4631` und `heltec_wifi_lora_32_V3` gebaut, RAM/Flash von
> `wiscore_rak4631` unveraendert. Auf Hardware noch zu pruefen (kein natives Repro moeglich,
> Timing-/Scheduler-Verhalten).

### N-17 — ESP32 `startNetwork()` can trip the task watchdog before its first feed — **VERIFIED (on real hardware)** — High

Not from the 2026-07-31 review or `docs/code-audit-20260712.md` — found 2026-08-18 flashing a Heltec V3
with today's fixes for hardware verification, reproduced deterministically, root-caused with
debug instrumentation on real hardware, and fixed same-day. Recorded here because it is a real,
previously-undetected production defect, independent of every other finding in this document.

`esp_task_wdt_add(NULL)` runs once, early in `esp32setup()` (`esp32_main.cpp:606`).
`esp_task_wdt_reset()` runs exactly once in the entire codebase — the first line of
`esp32loop()` (`esp32_main.cpp:1815`). `startNetwork()` (`udp_functions.cpp:490`) runs
synchronously **inside** `esp32setup()`, before `esp32loop()` ever starts feeding the watchdog,
and blocks on `WiFi.mode()` transitions (1.2 s of `delay()`) and an unbounded
`WiFi.scanNetworks()` call. On a board with `bGATEWAY`/`bEXTUDP`/`bWEBSERVER`/`bNETCONSOLE`
active and a real WiFi SSID configured, this synchronous block can exceed the default task
watchdog timeout — `task_wdt: Task watchdog got triggered ... loopTask`, `abort()`, reboot,
repeat, forever.

**Verified mechanism, not inferred**: debug-instrumented build on a Heltec V3 printed
`[DBG] pre-startNetwork` every boot but never the paired `post-startNetwork` — the watchdog
fired mid-scan, before `startNetwork()` could return. Confirmed pre-existing: `git diff
da3f7330..<pre-fix HEAD> -- src/udp_functions.cpp` shows zero hunks touching `startNetwork()`
before the fix — none of today's other fixes caused this.

> **STATUS 2026-08-18 — BEHOBEN.** `esp_task_wdt_reset()` (guarded `#if defined(ESP32)`) added
> at four points inside `startNetwork()`: before the WiFi mode-reset/delay sequence, immediately
> before and after `WiFi.scanNetworks()`, and before the final settle `delay(500)`. Feeds the
> watchdog through the legitimately slow boot work without changing its behaviour. Verified on
> real hardware: clean boot, WiFi connected, IP obtained, UDP gateway/HMAC console/ext-UDP all
> started, no watchdog/abort/reboot markers over an extended observation window. `pio test -e
native` green; `heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `t_deck`, `t_deck_pro` build clean.
> RAM unchanged, flash +28 B.

### N-18 — der SEC-02-Fix verhinderte jeden BLE-Verbindungsaufbau — **VERIFIED (auf echter Hardware, per Bisect)** — Critical

Kein Befund aus dem Review von 2026-07-31 und nicht aus `docs/code-audit-20260712.md`, sondern eine
**Regression aus unserem eigenen Fix**: `d36fb66f` (SEC-02) hat den BLE-Verbindungsaufbau
auf ESP32 vollstaendig unbrauchbar gemacht. Gefunden am 2026-08-18 beim Bisect gegen
pristine `upstream/dev`, behoben mit `bd10b636`.

SEC-02 ersetzte in `printfdeb()` (`src/printfdeb_functions.cpp`)

```c
Serial.printf(temp);            // unsicher: temp wird erneut als Format-String geparst
Serial.printf("%s", temp);      // der Fix -- sicher, aber teuer
```

Die Sicherheitsaussage ist korrekt. Der Nebeneffekt war es nicht: `Serial` ist auf ESP32
ueber `net_console.h:76` (`#define Serial MSerial`) letztlich `Print::printf`. Diese
Funktion formatiert in einen **64-Byte-Stackpuffer** und `malloc()`t fuer jede laengere
Ausgabe. Vorher wurden die `%`-Direktiven im fertigen Text ein zweites Mal geparst und die
Ausgabe war meist kurz genug fuer die 64 Byte — **es gab keine Allokation**. Der
Sicherheitsfix hat sie also erst eingefuehrt, und zwar bei nahezu jeder Log-Zeile.

Dieser Heap-Churn liess NimBLE keine mbufs mehr fuer den Verbindungsaufbau finden (der
Build nutzt `-DCONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4` statt der Standard-12). Der Node
beantwortete `CONNECT_IND` nicht mehr.

**Diagnostisch wichtig, weil es in die Irre fuehrt:** der Central sieht
`LE Connection Complete: Success`. Das beweist **nichts** ueber die Gegenstelle — dieses
Event erzeugt der Central lokal, sobald er `CONNECT_IND` gesendet hat. Danach lief
`LE Read Remote Used Features` in den Timeout und der Central brach nach ~250 ms mit
`Connection Failed to be Established (0x3e)` ab; BlueZ meldete das als
`le-connection-abort-by-local`, was nach einem Fehler auf der Central-Seite aussieht. Auf
dem Node feuerte weder `onConnect` noch `onDisconnect`, weil der NimBLE-Host nie von einer
Verbindung erfuhr.

> **STATUS 2026-08-18 — BEHOBEN** (`bd10b636`). `Serial.write((const uint8_t*)temp, len)`
> statt `Serial.printf("%s", temp)`: der Text wird nicht erneut als Format-String
> interpretiert (SEC-02-Eigenschaft bleibt vollstaendig erhalten) und es wird nicht mehr
> allokiert.
>
> Nachweiskette auf echter Hardware (Heltec V3 gegen einen BlueZ-Central):
> pristine `upstream/dev` (`8114d7ae`) verbindet — `9b07ea1d` (Commit vor SEC-02) verbindet —
> `d36fb66f` (SEC-02) 3/3 Fehlversuche — `d36fb66f` mit **nur** dem SEC-02-Hunk revertiert
> verbindet wieder (isoliert den Commit) — HEAD inklusive aller 62 Commits plus Fix
> verbindet. Builds `heltec_wifi_lora_32_V3` + `wiscore_rak4631` SUCCESS, native Tests 15/15.
>
> **Lehre fuer kuenftige Bisects:** der Startpunkt einer Sitzung ist keine bekannt-gute
> Referenz. `da3f7330` (Sitzungsbeginn) schlug ebenfalls fehl und fuehrte zunaechst zu dem
> falschen Schluss "liegt nicht an uns" — er war Commit 43 von 62 ueber `upstream/dev`. Erst
> der Merge-Base war die echte Referenz.
>
> **Regel daraus:** `printf("%s", bereits_formatiert)` nie auf einem heissen Pfad. Fuer
> fertige Strings `write()` benutzen — sicher _und_ allokationsfrei.

### BATT-01 — `read_batt()` blockiert die Hauptschleife ~100 ms alle ~500 ms — **VERIFIED (auf echter Hardware)** — Medium

Ebenfalls neu, gefunden 2026-08-18 bei der Instrumentierung waehrend der N-18-Suche.
Vorbestehend (zuletzt angefasst 2026-08-03, `c78adcc5`), unabhaengig von N-18.

Der Heltec-V3/V4/Stick-Zweig von `read_batt()` (`src/batt_function_old.cpp`) schaltet den
ADC-Spannungsteiler ein und wartet dessen Einschwingzeit mit einem blockierenden
`delay(100)` ab. Aufgerufen wird die Funktion ueber den `BattTimeWait`-Zweig in
`esp32loop()` alle ~500 ms — die Hauptschleife steht damit dauerhaft rund ein Fuenftel der
Zeit still. Auf Hardware gemessen: `[DBGSTALL] seg read_batt took 100617 us`,
`loop gap 107000 us`, reproduzierbar im 500–600-ms-Takt.

> **STATUS 2026-08-18 — BEHOBEN** (`b44fe712`). Einschwingzeit ueber zwei Aufrufe verteilt
> statt zu blockieren: erster Aufruf schaltet den Teiler frei und merkt sich den
> Zeitstempel, spaetere Aufrufe messen erst nach echten >=100 ms; bis dahin gilt der zuletzt
> gemessene Wert. Messverhalten und Ergebniswert unveraendert, die Messkadenz geht von 500 ms
> auf 1000 ms. Nach dem Fix verschwinden die Stall-Meldungen vollstaendig.
>
> **Abgrenzung:** war **nicht** die Ursache von N-18 — nach diesem Fix allein trat der
> BLE-Fehler unveraendert auf. Der Stall ist fuer sich genommen real und wurde deshalb
> behoben.

### CFG-01 — `[nrf52]`-Sektion kollidiert namensgleich ueber alle drei `variants/*/platformio.ini` — **FIXED (`24122ed4`)** — Medium (Build-Isolation)

> **STATUS 2026-08-22: Root cause GEFIXT.** Die sechs Keys stehen jetzt genau
> einmal als `[nrf52_base]` in der Root-platformio.ini; die drei Env-Sections
> extenden sie explizit. Beweis der Verhaltensgleichheit: `pio project
metadata` vor/nach fuer alle drei Envs bitgleich (defines/includes/Flags);
> Clean-Builds gruen (t114/t_echo bauen dabei mit dem mitgeerbten `-Werror` —
> der Wave-0.2-Rest fuer diese Boards war durch den Merge faktisch laengst
> aktiv). Die absichtlich weiter geerbten RAK-Anteile (BOARD_RAK4630,
> wiscore-Quellfilter samt nie gebautem t_echo-PIN_LED3-Init) sind im
> Root-Kommentar dokumentiert; ihre Entkopplung bleibt ein eigenes Vorhaben
> mit t114/t_echo-Hardware.

Neu, gefunden 2026-08-20 beim Versuch, Wave 0.2 (`-Werror`) auch fuer `heltec_t114`/`t_echo`
zu aktivieren.

`platformio.ini:14` laedt `extra_configs = variants/*/platformio.ini`. Jede der drei
nRF52-Varianten-Dateien deklariert eine Sektion `[nrf52]` mit demselben Namen
(`variants/wiscore_rak4631/platformio.ini:1`, `variants/heltec_t114/platformio.ini:1`,
`variants/t_echo/platformio.ini:1`). Solange nur `wiscore_rak4631`s `[nrf52]`-Sektion
zusaetzliche Keys trug (`build_src_filter`, `build_flags` inkl. `-D BOARD_RAK4630=...`),
gewannen deren Werte fuer alle drei `env:*`, die per `extends = nrf52` darauf verweisen —
`heltec_t114` und `t_echo` bekamen `BOARD_RAK4630` in ihre Compile-Kommandos gemischt,
obwohl keines der beiden das WisBlock-RAK-Board ist (verifiziert per `-v`-Build-Log-Diff
zwischen gestashtem und aktuellem Baum, mit vollstaendig geleertem `.pio/build/`).

Das blieb unbemerkt, weil drei Stellen `#ifndef BOARD_RAK4630` als Proxy fuer "ist ESP32"
benutzten (`adc_functions.cpp:3`, `batt_functions.h:40`, `batt_function_old.cpp:10`/`:69`
— siehe C-Symptome unten) — mit dem geleakten Define kam dort nie echter ESP-IDF-Code
(`esp_adc_cal.h`) zum Tragen, die Boards "bauten einfach durch".

**Symptome behoben (Commit `b0c3d8c0`):** die drei Guards von `BOARD_RAK4630` auf die
tatsaechliche Plattform umgestellt (`defined(ESP32)` bzw. `!defined(NRF52_SERIES)`, im
selben File bereits als Diskriminator etabliert). Dabei zusaetzlich gefunden:
`batt_function_old.cpp:69` nahm `BOARD_T_ECHO` bereits explizit aus dem
ESP-IDF-Kalibrierungstyp-Block aus, aber nie `BOARD_HELTEC_T114` — jemand hatte das
Problem fuer `t_echo` offenbar schon einmal von Hand gepatcht, nur unvollstaendig. Fuer
alle Boards verifiziert bit-identisch zum vorherigen (durch den Leak maskierten)
Verhalten.

**Root cause NICHT behoben.** Die Sektionskollision selbst ist gefaehrlicher als ihre drei
bekannten Symptome: JEDE kuenftige Aenderung an einer `[nrf52]`- oder `[esp32]`-Sektion in
einer `variants/*/platformio.ini` kann auf dieselbe Art in andere Boards derselben Familie
durchsickern, abhaengig davon, welche Datei zuletzt geladen wird und welche Keys sie neu
definiert — aus dem Diff einer einzelnen Datei nicht ersichtlich.

**Fix (nicht in dieser Session):** jede Sektion eindeutig benennen
(`[nrf52_rak4631]`, `[nrf52_t114]`, `[nrf52_techo]`; analog fuer `[esp32]`, falls die ESP32-
Varianten-Dateien dieselbe Kollision haben — nicht separat geprueft) und `extends`
entsprechend anpassen. Sollte mit allen betroffenen Boards am Bankarbeitsplatz verifiziert
werden, nicht blind.

**Trigger fuer erneutes Aufgreifen:** `heltec_t114` oder `t_echo` an Hardware verfuegbar
(dann Wave 0.2-Rest dort nachholen), oder die naechste Aenderung an einer
`[nrf52]`/`[esp32]`-Sektion in `variants/*/platformio.ini`.

### N-19 — `--dfu` haengt sich auf, statt in den UF2-Bootloader zu wechseln — **FIXED (`b03b9a27`, auf echter Hardware verifiziert)** — High

> **STATUS 2026-08-21: GEFIXT.** `enterUf2Dfu()` wird nicht mehr gerufen. Stattdessen
> setzt der `bEnterDfu`-Zweig GPREGRET selbst per SoftDevice-SVC
> (`sd_power_gpregret_clr/set(0, 0x57)` — bei aktivem SoftDevice erlaubt) und faellt in
> den bewaehrten `NVIC_SystemReset()` durch. Eingrenzung per Ausschluss auf Hardware:
> derselbe `reset_mcu()`-Pfad funktioniert aus dem TinyUSB-Task (1200-Baud-Touch, zweimal
> live als Fernrettung genutzt), und `NVIC_SystemReset()` funktioniert aus exakt diesem
> Loop-Pfad (`--reboot` getestet: Echo, Port weg, Neu-Enumeration) — uebrig bleibt
> `sd_softdevice_disable()` im Loop-Kontext als Verdaechtiger (Core-Interna nicht weiter
> seziert). Wichtig: `Serial.flush()` + `delay(300)` vor dem Reset ist funktional noetig —
> ohne Wartezeit kam das Board trotz Readback `GPREGRET==0x57` als App statt als
> Bootloader zurueck. Verifikation: `--dfu` zweimal in Folge → Bootloader-PID `0x29`,
> `/Volumes/RAK4631` gemountet; Rueckweg beide Male per `firmware.uf2`-Kopie → App laeuft.
> Der komplette Fern-Flash-Zyklus, fuer den der Befehl gebaut wurde, ist damit bewiesen.

Neu, gefunden 2026-08-21 beim ersten Hardware-Test von `--dfu` (Commit `7bac915a`,
2026-08-19 committet, dort selbst als "auf Hardware noch ungetestet" vermerkt — dies ist
genau dieser erste Test).

`--dfu` ueber Seriell gesendet. Ergebnis: die serielle Konsole verstummte vollstaendig
(fuenf unabhaengige Leseversuche ueber ~50 s — `cat`, `stty`+`cat`, `pio device monitor`,
`pyserial` mit gesetztem DTR/RTS, verlaengertes passives Lauschen — durchweg 0 Bytes), der
USB-Deskriptor zeigte aber weiterhin die Applikations-PID `0x8029` (`ioreg`), nie die
Bootloader-PID `0x0029`, und `/Volumes/RAK4631` erschien nie. Weder normaler Betrieb noch
Bootloader — das Board haengt zwischen beiden Zustaenden.

`enterUf2Dfu()` (`cores/nRF5/wiring.c:98` im Adafruit-Core) ruft `sd_softdevice_disable()`
vor `NVIC_SystemReset()` auf; ob genau das der Haenger-Punkt ist, ist **nicht verifiziert**
— nur das Symptom ist belegt. Der aufrufende Codepfad (`bEnterDfu`-Zweig in `nrf52loop()`,
`nrf52_main.cpp`, ueber den bestehenden `rebootAuto`-Mechanismus) liegt ausserhalb jeder in
der Session vom 2026-08-20/21 neu eingefuehrten Critical Section — der Fund ist also nicht
durch `N-16`/`CONC-15`/`CONC-16`/`CONC-17` verursacht.

**Recovery verifiziert:** ein einzelner physischer Tastendruck auf Reset holt das Board
vollstaendig zurueck — wieder Applikations-PID, serielle Konsole sofort wieder aktiv,
RX/TX/Settings unveraendert intakt. Kein Datenverlust, kein Soft-Bricking. (Stand vor
dem Fix; inzwischen sind `--dfu` und der 1200-Baud-Touch als Fernwege verifiziert, siehe
STATUS-Box.)

**Fix:** siehe STATUS-Box oben (`b03b9a27`). Die exakte Core-interne Ursache des
`sd_softdevice_disable()`-Haengers aus dem Loop-Task bleibt unverifiziert — der Fix
umgeht die Funktion, statt sie zu reparieren.

---

### N-20 — Netzwerk-Pfade (W5100S) frieren den Loop-Task ein auf Gateway-Nodes ohne Ethernet-Hardware/Link — **Hauptausloeser GEFIXT (`780df254`), Rest offen** — High

> **STATUS 2026-08-21 (abends): Hauptausloeser gefixt, auf Hardware mit RAK13800
> verifiziert (`780df254`).** Zwei Aenderungen: (1) `startETH()` prueft den
> Link-Status VOR dem blockierenden `Ethernet.begin(mac, 10000UL)` — begrenzt auf
> LinkON wartend (max. 3 s, die PHY-Aushandlung nach dem HW-Reset braucht mehrere
> Sekunden; ein Sofort-Check meldete dauerhaft LinkOFF und verhinderte jede
> Wiederverbindung). (2) Der periodische Reconnect in `nrf52loop()` ruft
> `resetDHCP()` statt `initethDHCP()` — das alte volle HW-Init resettete den
> W5100S bei jedem Versuch und haette mit dem Link-Check eine Endlosschleife aus
> PHY-Reset und LinkOFF ergeben (auf Hardware beobachtet). Verifiziert: ohne Link
> "link OFF - skip DHCP" in <=3 s bei responsivem Loop; mit Link DHCP-IP,
> KEEP-Heartbeats zum OE-Server, Webserver HTTP 200. **Soak-Test bestanden
> (2026-08-21, 12:01–12:06):** Kabel im Betrieb gezogen und nach 5 min wieder
> gesteckt — kein Freeze, kein Reboot (Uptime lief durch), DHCP automatisch neu
> bezogen, KEEP-Heartbeats und Webserver danach wieder aktiv. **Noch offen:** die
> exakte Fundstelle des "mark=5"-Freezes (posinfo/heyinfo/telemetry-Abschnitt) ist
> weiterhin unbenannt; die W5100S-Bibliotheks-Warteschleifen selbst (Socket-Ops,
> `maintain()`) sind ungehaertet.
>
> **BACKLOG (2026-08-22): W5100S-Warteschleifen-Haertung + Fault-Injection-Test.**
> Restrisiko: Link-Verlust MITTEN in einer Socket-Operation (`beginPacket`/
> `write`/`endPacket` in `sendUDP()`, `parsePacket` in `getUDP()`, interne
> Status-Polls der RAK13800-Bibliothek) kann den Loop-Task weiterhin
> nichtdeterministisch haengen lassen — der bestandene Soak-Test war EIN
> Durchlauf mit EINEM Timing. Testrezept (das reine "UDP-Frames zum Node
> senden waehrend unplug/replug" reicht nicht):
>
> 1. **Bidirektionale Dauerlast**, damit der Flap eine aktive Socket-Op
>    trifft: Mesh-Nachrichten im 1–2-s-Takt (Gateway laedt jede per DATA
>    hoch), EXTUDP on (zusaetzlicher Sendepfad), Server-Rueckverkehr
>    (BEAT/GATE) laeuft mit.
> 2. **Viele Flaps mit zufaelliger Phase**: Kabel ≥20-mal ziehen/stecken,
>    Haltezeiten variieren (2 s bis 2 min) — der historische Haenger war
>    nichtdeterministisch (ein Boot hing minutenlang, der naechste lief).
> 3. **Loop-Alive-Monitor** parallel auf Serial: periodisches Kommando-Echo
>    (z. B. `--pos` alle 10 s); Stall > 5 s = Befund. Nach dem Lauf
>    `RESETREAS` pruefen (0x4 = Absturz statt Haenger).
>
> Haertungsrichtung bleibt wie dokumentiert: Netzwerk-Abschnitte an
> `Ethernet.linkStatus()` koppeln, bevor Socket-Ops laufen; Timeouts um die
> RAK13800-Statusschleifen (Bibliotheks-Fork oder Wrapper).
>
> **Erster Teil umgesetzt (2026-08-22, `b62976c9`/N-23):** der Loop-seitige
> `startWebserver()`/`startExternUDP()`-Restart laeuft nur noch bei
> `neth.hasIPaddress` — die Socket-Op-auf-uninitialisiertem-Chip-Falle ist
> damit zu. Die Bibliotheks-Warteschleifen (Ops auf initialisiertem Chip bei
> Link-Verlust mitten in der Operation) bleiben der offene Rest dieses
> Backlogs.
>
> **Fault-Injection-Soak durchgefuehrt (2026-08-22, BESTANDEN):** Testaufbau
> exakt nach obigem Rezept — Mock-Peer auf dem Mac (Harness nach doc 11 §3:
> Listener :1799, DM-Injektor alle 10 s mit Heltec-ACK-Rueckverkehr,
> Serial-Echo-Probe alle 10 s) plus compile-gated 500-ms-Sequenz-Heartbeat
> aus dem Loop-Task (`MC_TEST_HOOKS` in `getExternUDP()`, eingecheckt,
> in Produktionsbuilds nicht enthalten). Ergebnis ueber 5 Kabel-Flaps
> (Haltezeiten 4–16 s, mitten im Verkehr): **der Loop-Task blockierte
> keinen einzigen Takt** — waehrend jeder Unplug-Phase lief die
> Heartbeat-Sequenz im exakten 500-ms-Raster weiter (verlorene seq ==
> Fensterdauer/0,5 s), Serial-Echo-Stalls: 0, seq-Luecken ausserhalb der
> Fenster: 0, kein Reboot (Uptime durchgehend), Reconnect nach Replug
> automatisch. Zusammen mit dem 5-Minuten-Soak vom 21.08. ist die
> Testverpflichtung dieses Backlogs erfuellt; die W5100S-internen
> Warteschleifen bleiben als theoretisches Restrisiko dokumentiert
> (Upstream-Bibliotheks-Thema), ein Blocking wurde unter Last nicht mehr
> beobachtet.

Neu gefunden 2026-08-21 (als Stoerfaktor bei der N-19-Verifikation), am selben Tag
nachmittags per Instrumentierung auf die Loop-Abschnitte eingegrenzt. Vorbestehend,
unabhaengig von allen Fixes dieser Kampagne. **Schluckt N-21** (siehe dort): die
"CDC-Tod"-Symptome sind in Wahrheit dieser eingefrorene Loop-Task.

Auf einem als Gateway konfigurierten RAK4631 (`bGATEWAY`, `bEXTUDP`, Webserver on)
**ohne** funktionierende Ethernet-Hardware/Link belegt:

1. **Setup-Blockade, nichtdeterministisch:** Ein Boot blieb nach `Initialize Ethernet`
   (`nrf_eth.cpp`, `startETH()` → `Ethernet.begin(mac, 10000UL)`) **minutenlang**
   stehen; der naechste Boot mit identischer Firmware lief in Sekunden durch.
2. **Periodische Loop-Stalls 8–12 s** etwa alle 60–70 s (belegt per
   `RX_TIMEOUT_FIRE delta=12426…13920` statt normal `4582`) — der
   `resetDHCP()`-Pfad aus `sendUDP()` (nach `MAX_ERR_UDP_TX` Fehlversuchen) laeuft in
   dasselbe blockierende `startETH()`.
3. **Minutenlange bis dauerhafte Loop-Freezes im Betrieb**, per
   Breadcrumb-Instrumentierung (Loop-Herzschlag + Abschnittsmarken, Freeze-Meldung aus
   dem Timer-Service-Task ueber rohe `tud_cdc_n_write`) zwei Fundstellen benannt:
   - **Gateway-Block 1** (`neth.getUDP()`/`sendUDP()`-Abschnitt in `nrf52loop()`):
     ≥20 s Freeze beobachtet.
   - **posinfo/heyinfo/telemetry-Abschnitt**: ueber **2 Minuten** durchgehend
     eingefroren (Meldung alle 5 s mit unveraendertem Herzschlag), danach voll stumm.
     Beide Abschnitte enden in W5100S-Socket-/SPI-Operationen der RAK13800-Bibliothek;
     auf abwesender/linkloser Hardware liefern SPI-Reads Muell und die internen
     Statuswarteschleifen der Bibliothek kehren nichtdeterministisch nicht zurueck.

**Folgewirkungen des eingefrorenen Loop-Tasks** (alles 2026-08-21 einzeln belegt):
serielle Kommandoverarbeitung tot (`checkSerialCommand()` laeuft nicht), keine Echos,
LoRa-TX-Ring wird nicht bedient — waehrend `OnRxDone`-Debugzeilen (Timer-Service-Task)
weiter stroemen ("TX lebt, RX tot") und BLE (SoftDevice) weiterlaeuft. Der
1200-Baud-Touch (TinyUSB-Task) funktioniert in jedem beobachteten Freeze-Zustand und
bleibt der verlaessliche Fernrettungsweg.

**Verstaerker (gefixt, `1855cb3e`):** war der CDC-TX-FIFO im Freeze-Moment voll,
blockierte jeder weitere `printfdeb()` aus dem Timer-Task in
`Adafruit_USBD_CDC::write()` endlos → auch die Timer-Task-Ausgaben starben ("voll
stumm"). Die printfdeb-Familie wartet jetzt begrenzt (20 ms) und verwirft dann.

**Nicht gefixt:** die W5100S-Blockaden selbst. Fix-Richtungen: Netzwerk-Abschnitte an
`hasETHHardware`/Link-Status koppeln statt sie auf Muell-SPI laufen zu lassen;
Timeouts in den RAK13800-Statusschleifen. Braucht fuer den sauberen Beweis einmal
echte Ethernet-Hardware (DRY-21-Umfeld). Workaround fuer Bench-Tests: Node nicht als
Gateway/EXTUDP konfigurieren, oder Kommandos ausserhalb der Stall-Fenster senden.

### N-21 — ~~USB-CDC-RX-Richtung stirbt im Betrieb~~ → kein USB-Defekt, Symptom von N-20 — **RESOLVED als Duplikat (auf echter Hardware bewiesen)** — Medium

Beobachtet 2026-08-21 ("CDC-RX tot, TX lebt weiter"; dieselbe Symptomklasse
motivierte `--dfu` in `7bac915a`). Die Untersuchung am selben Tag hat den USB-Stack
vollstaendig entlastet — jede Schicht einzeln auf der Hardware geprueft:

- **EP0/Control-Pfad:** 1200-Baud-Touch (SET_LINE_CODING + Line-State-Callback)
  funktionierte in jedem beobachteten "toten" Zustand, mehrfach als Fernrettung genutzt.
- **Line-State:** Uebergangs-Ring (Dateisystem-Postmortem) zeigte DTR-Drops und
  -Asserts, die sauber ankamen und verarbeitet wurden; im "toten" Fenster stand
  `conn=1 ls=0x03 sus=0 mnt=1` — CDC-Klassenzustand voellig gesund.
- **Bulk-OUT:** Host-Bytes erreichten den RX-FIFO auch im toten Zustand (300-Byte-Probe
  fuellte `avail>=200` und loeste den Test-Watchdog aus).
- **Suspend-Flag, Endpoint-Wedge, macOS-Treiber:** alle als Ursache ausgeschlossen
  (Telemetrie `sus=0`, `awr=256` — TX-FIFO leer, es schrieb schlicht niemand mehr).

Tatsaechliche Ursache: **der Loop-Task war eingefroren** (W5100S-Pfade, siehe N-20) —
`checkSerialCommand()` lief nicht mehr (RX "tot"), waehrend die
`OnRxDone`-Debugzeilen aus dem Timer-Service-Task weiterliefen (TX "lebt").
Kein CDC-, TinyUSB- oder Treiber-Bug. Fix = Fix von N-20; der Verstaerker
(blockierendes `write()` bei vollem FIFO) ist mit `1855cb3e` entschaerft.

Lehre fuer kuenftige Diagnosen: "seriell tot bei lebendem USB-Deskriptor" zuerst als
Loop-Freeze pruefen (Herzschlag-Breadcrumb), nicht als USB-Problem. Und: LittleFS-
Schreibzugriffe aus dem Timer-Service-Task crashen das Board reproduzierbar in einen
Boot-Loop (waehrend der Untersuchung selbst ausgeloest und behoben) — Dateisystem nur
aus dem Loop-Task anfassen.

### N-22 — ~~EXTUDP crasht den nRF52-Gateway~~ → Stack-Overflow im Loop-Task auf dem Nachrichtenpfad — **FIXED (`9ce62aa0`, auf echter Hardware gemessen und verifiziert)** — High

> **STATUS 2026-08-21 (abends): GEFIXT, root cause gemessen.** Kein EXTUDP-Logik-Bug:
> der Loop-Task auf nRF52 hat 4 KB Stack (`LOOP_STACK_SZ` im Adafruit-Core, hart
> codiert), und der Pfad `checkSerialCommand()` → `sendMessage()` → `sendExtern()`
> erreichte `uxTaskGetStackHighWaterMark(NULL) == 0` — Stack vollstaendig
> aufgebraucht, Nachbar-RAM ueberschrieben, Crash Sekunden spaeter. EXTUDP war nur
> der Ausloeser, weil `sendExtern()` die Pfadtiefe ueber die Kante schob; das
> Nebensymptom "JSON-Datagramme kommen trotz rc=1-Sendekette nie an" gehoerte zum
> selben Schadensbild. Eingrenzung: Peer-Verhalten per stillem Live-Listener
> ausgeschlossen, Socket-Lebenszyklus instrumentiert (alle rc ok), dann
> Watermark-Messung. Fix nach dem `1951aa7d`-Muster: die grossen Puffer des Pfads
> auf nRF52 in BSS (`sendMessage()`: 200+200+300 B; `checkSerialCommand()`: 600 B) —
> Watermark am selben Punkt danach **248 Woerter (~1 KB) frei**. Verifiziert auf dem
> Gateway mit EXTUDP on: mehrere Nachrichten ohne Crash, `[EXT] Out`/`TX-UDP`/
> `TX-LoRa` laufen, Datagramme kommen beim Peer an (nc-Listener). **Workaround
> aufgehoben:** EXTUDP ist auf dem Bench-RAK wieder on, EXT IP wieder
> 192.168.68.64. Merkposten fuer Upstream: `LOOP_STACK_SZ` ist mit 4 KB fuer diese
> Firmware knapp bemessen — jede weitere Vertiefung des Nachrichtenpfads (weitere
> `printfdeb`-Frames à ~900 B!) kann die Kante erneut reissen; `uxTaskGetStackHighWaterMark`
> gehoert in kuenftige Bench-Diagnosen.

Neu, gefunden 2026-08-21 nachmittags bei der Verifikation von `DRY-21`/`623c4c0e` —
erst erreichbar, seit der Bench-RAK4631 echtes Ethernet mit Link hat. Vorbestehend,
durch Ausschluss von allen Kampagnen-Fixes getrennt (Crash reproduziert auf exakt
`623c4c0e` UND auf Vorstaenden; Breadcrumbs in `sendUDP()`/`getUDP()` blieben stumm —
der Absturz liegt VOR beiden).

Symptom: RAK4631 als Gateway (`bGATEWAY`, `bEXTUDP` on, `EXT IP 192.168.68.64`,
Ethernet verbunden) rebootet **reproduzierbar ~2–4 s nach jedem eigenen
Nachrichtenversand** (`::{9999}…` ueber Seriell). Letzte Ausgabe vor dem Reset:
`[EXT] Out:`-JSON + BLE-Ringpuffer-Dump; danach `RESETREAS=0x00000004` (SREQ — der
Weg des SoftDevice-Fault-Handlers nach einem App-Absturz). Kein Watchdog, kein
Lockup-Bit.

**Isolation per Toggle:** `--extudp off` → identische Nachricht laeuft sauber durch
(TX-UDP decodiert, TX-LoRa raus, 30 s stabil, zweifach wiederholt). `--extudp on` →
Reboot. Der Absturz liegt damit im EXTUDP-Pfad (`extudp_functions.cpp` /
`getExternUDP()`/`flushExternQueue()`/`sendExtern()` auf dem W5100S-Unterbau) —
root cause nicht weiter eingegrenzt (eigene Session; Kandidaten: Verarbeitung der
ICMP-Port-Unreachable-Antwort eines toten EXT-Peers, Puffer im JSON-Pfad,
Socket-Zustand des zweiten UDP-Sockets).

**Workaround (historisch, aufgehoben):** `--extudp off` war vom Nachmittag bis zum
Fix gesetzt. Boot-Log zeigt seit `6003e90c` die Reset-Ursache
(`[BOOT] RESETREAS=…`), was kuenftige Vorfaelle dieser Klasse sofort als Absturz
ausweist — und bei genau dieser Diagnose der Schluessel war.

### N-23 — `--extudp on` ohne Gateway/Webserver brickt den nRF52-Node dauerhaft — **FIXED (`b62976c9`, auf Hardware reproduziert und verifiziert)** — High

Neu gefunden 2026-08-22 bei der Bench-Verifikation des N-12-Fixes (nach
`--cleanflash` wurden die Restore-Kommandos in der Reihenfolge `--extudp on`
vor `--gateway on` gesendet — der Node fror nach dem ersten Kommando ein und
blieb es ueber Reboots und Neuflashes hinweg).

Mechanismus: Setup initialisiert die Ethernet-Hardware nur bei
`bGATEWAY || bWEBSERVER` (`nrf52_main.cpp:1074`). Der 15-Minuten-Restart-Block
im Loop laeuft aber bei `bWEBSERVER || bEXTUDP` und feuert wegen
`web_timer == 0` sofort im ersten Durchlauf — `startExternUDP()` traf den
NICHT initialisierten W5100S, dessen Socket-Ops (`UdpExtern.begin()` /
`sendExternHeartbeat()`) nie zurueckkehren. Loop-Task tot (kein Echo, kein
Netz), und weil `bEXTUDP` gespeichert war, wiederholte sich das bei jedem
Boot: eine **persistente Konfigurations-Falle**, die nur der
1200-Baud-Touch + Neuflash + Fix durchbrach. Diagnose-Merkposten: Setup lief
bis "[EXT]...now sending" durch, danach Stille — der Haenger sass in
`sendExternHeartbeat()`. Ein Bisect (N-14-Aenderungen gestasht) entlastete
den parallelen Ring-Umbau eindeutig.

Fix: `startWebserver()`/`startExternUDP()` im Loop-Restart-Block nur noch bei
`neth.hasIPaddress`. Verifiziert: exakt die Brick-Konfiguration bootet und
antwortet; mit Gateway on danach voller Betrieb. Dies ist der erste Teil des
N-20-Backlogs (Netzwerk-Pfade an Link-/HW-Status koppeln); die
W5100S-Bibliotheks-Warteschleifen selbst bleiben ungehaertet (siehe
N-20-BACKLOG-Box).

---

### N-24 — Indirekte Prio-Eviction verwaiste einen belegten TX-Ring-Slot — **FIXED (`cf74e08e`, von der neuen Testsuite gefunden)** — High

Gefunden am Entstehungstag der nativen TX-Ring-Suite (`test/test_txring/`,
QA-Welle 2026-08-22): die beiden Ueberlauf-Pfade in `addTxRingEntry()`
stimmten sich nicht ab. Die Prio-Eviction raeumt den Slot mit der
schlechtesten Prioritaet IRGENDWO im Fenster; der anschliessende unbedingte
"ring full: advance read pointer"-Block prueft nur die Index-Kollision.
Raeumte die Eviction einen Mittel-Slot, blieb der Slot an `iRead` belegt,
`iRead` wurde trotzdem darueber hinweggeschoben — eine gueltige, nie
gesendete Nachricht (moeglicherweise CRITICAL) fiel dauerhaft aus dem
Scan-Fenster von `getNextTxSlot()`, ohne `stat_drop_count`-Buchung.
Vorbestehend (identisch vor dem N-14-Umbau); deterministisch reproduziert
im Test `test_n24_indirekte_eviction_verwaist_keinen_slot`.

Fix: raeumt die Eviction einen Slot ungleich `iRead`, zieht der Eintrag von
`iRead` in den freigewordenen Slot um (Slot-memcpy + Seitenarrays), der
`iRead`-Slot wird geleert und regulaer weitergerueckt. Preis: FIFO-Rang-
Verlust innerhalb gleicher Prioritaet, akzeptiert gegen Totalverlust.
Nebenfund ebenfalls gefixt (`a0bcd9da`): `clearSlotFirst` putzte nur 256
von 260 Slot-Bytes. Beleg fuer den Zweck der QA-Welle: die erste native
Suite ueber dem Ring fand am ersten Tag einen echten High-Defekt.

### N-25 — GPS-Baudscan loest den Task-Watchdog aus und schickt den Knoten in den Boot-Loop — **FIXED (Wellen 0 bis 3, auf echter Hardware reproduziert und verifiziert)** — Critical

Vollstaendige Analyse: `docs/bug-N25-gps-baud-scan-watchdog.md`. Kurzfassung:
`4c21cb49` (Audit-Befund C3) abonnierte den Task-Watchdog in der ersten Zeile
von `esp32setup()`. `WZ_GPS_Init()` laeuft aber aus `esp32loop()`
(`esp32_main.cpp:2706`) und blockiert dort ohne eine einzige Fuetterung rund
16 s (acht Baudraten x 1500 ms, dazu `GPSprobe()` mit unbegrenztem
`readUBX()`-Schwanz). Bei `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` bricht der Knoten
zwei bis drei Baudstufen nach Scan-Beginn ab. Da `--gps on` vor dem Absturz
persistiert wird, wiederholt sich das bei jedem Boot: Dauer-Boot-Loop, kein
Kommandofenster, Rettung nur per Reflash.

Am 2026-08-22 auf einem Heltec V3 mit u-blox-Modul (RX=47/TX=48) exakt
reproduziert: Abbruch 4,9 s nach Scan-Beginn nach 1200/2400/4800 Baud. Der
gegen die passende ELF symbolisierte Backtrace bestaetigt Abschnitt 2 des
Dokuments — `panic_abort` <- `esp_system_abort` <- `abort` <- `task_wdt_isr`,
der zweite Stack ist der unterbrochene `IDLE0`. Damit ist der Befund auf einem
zweiten Board unabhaengig belegt, nicht mehr nur auf der gemeldeten Supreme.

Fix in drei Schritten: S1 verschiebt die Subskription ans Ende von
`esp32setup()`; S4 fuettert den GPS-Init-Pfad ueber den einzigen Helfer
`src/watchdog_feed.h` (ausserhalb ESP32 ein No-op); S5 bricht den Scan bei der
ersten gueltigen NMEA-Pruefsumme ab und ersetzt den Argmax ohne Mindestanzahl
(B-15). Erkennungsdauer auf dem Heltec V3 von 12 000 ms auf 2120 ms.

Welle 3 hat die tote ISR-Variante geloescht (A-1 bis A-4); dabei kam heraus, dass
`gps_functions.cpp:24` die Variantenoption `GPS_BAUDRATE_SOFTCHECK` aus 15
`variants/*/configuration.h` unbedingt ueberschrieben hat -- der zweite Zweig
war deshalb auf ALLEN Boards unerreichbar, nicht nur auf den meisten. A-9 wurde
dabei widerlegt. Flash-Groesse danach byteidentisch, was den toten Code belegt.

Offen und ausdruecklich zurueckgestellt, nicht stillschweigend uebersprungen:
Welle 4 (S2, die vier unbegrenzten
Schleifen B-1/B-2/B-6/B-10 und der AP-Zweig B-13), Welle 5 (Coredump-
Partition), Welle 6 (B-1 bis B-15 als Katalogeintraege). Ebenso offen die
Hardware-Pruefungen 8.4 (Board mit `--gps on` ohne Modul) und 8.5
(Batteriestart ohne USB) — siehe `docs/gps-sensor-bench-20260822.md`.

### N-26 — RAK-GPS-Pfad merkt sich das Fehlschlagen der Erkennung nicht — **VERIFIED (auf echter Hardware, Kontrollfall gegen Upstream)** — Medium

`nrf52_main.cpp:729-784` probiert fuer `ENABLE_RAK_GPS` fest 9600, dann 38400
Baud. Schlaegt `myGPS.begin(Serial1)` in beiden Faellen fehl, wird
`"GPS: speed not found"` gedruckt — und danach **bedingungslos** `SetupUBLOX()`
gerufen (`:781`). Es gibt keine Variable, die den Fehlschlag festhaelt, und
keinen fruehen Ausstieg. Der Knoten sendet `UBX_SET_GNSS` und `UBX_MON_VER` ins
Leere und druckt ein leeres `[GPS_VER]`.

Das ist die nRF52-Entsprechung zu B-15: dort wird eine Phantom-Baudrate
erkannt, hier wird das Nicht-Erkennen gar nicht erst vermerkt. `gpsDetected`
wird auf diesem Pfad nie gesetzt.

Kosten: auf einem RAK4631 ohne GPS-Modul rund 9 s Boot-Zeit
(2 s Power-Cycle `WB_IO2`, 2 x ~3,3 s Probe, ~2 s `SetupUBLOX()` inklusive
`WaitPause()`), gemessen am 2026-08-22 auf DK5EN-90. Kein Absturz: auf nRF52
ist ueberhaupt kein Task-Watchdog scharf, N-25 trifft diesen Pfad also nicht.

Nebenbefund derselben Stelle: zweimal `while (!Serial1);` ohne Zeitgrenze
(`:741`, `:760`) — dieselbe Form wie N-09. Auf dem Adafruit-nRF52-Core liefert
`Uart::operator bool()` konstant `true`, die Schleifen sind daher heute inert.

Der Pfad ist **nicht** unser Regress: Upstream-Release `v4.35p.08.20`
(`wiscore_rak4631.zip`) auf demselben Board zeigt Zeile fuer Zeile dasselbe
Verhalten. Ebenso wenig ist es ein Firmware-Defekt, dass auf diesem Board kein
GPS gefunden wurde — `--showi2c` meldet unter beiden Firmwares
`no devices found`, das Modul ist schlicht nicht erreichbar.

Dritte Kopie derselben Logik: `sendUBXCommand()`, `WaitPause()`, `startTimeout`
und `ver` existieren in `nrf52_main.cpp:2546-2580` ein zweites Mal neben den
Originalen in `gps_functions.cpp`. Genau der Driftmechanismus aus A-1.

### N-27 — BME680-Treiber haelt ein blosses I2C-ACK fuer eine Chip-Erkennung — **FIXED (auf echter Hardware reproduziert und verifiziert)** — Medium

`bme680.cpp:62-89` prueft mit `Wire.beginTransmission(0x76)` /
`endTransmission()`, ob unter der Adresse ueberhaupt jemand antwortet, und
setzt daraufhin `bme680_found = true`. Der anschliessende `bme.begin(...)`
(`:95`, `:98`) wird **ohne Auswertung des Rueckgabewerts** gerufen. `begin()`
liest aber die Chip-ID und ist genau die Stelle, die einen BME680 von einem
BME280/BMP280 unterscheidet — 0x76 und 0x77 teilen sich alle drei Chips, wie
der Kommentar in `:48` selbst festhaelt.

Folge auf einem Board mit BME280 an 0x76 und `--680 on`: der Node meldet
`[INIT]...BME680 sensor found at 0x76`, `--wx` zeigt `BME680: on (found)`, und
jeder Lesezyklus druckt `Failed to complete reading :(` — dauerhaft, ohne dass
irgendetwas den Zustand zurueckstellt. Am 2026-08-22 auf einem Heltec V3 mit
BME280 an 0x76 reproduziert.

Fix: `bme680_found` wird jetzt aus dem Rueckgabewert von `bme.begin()` gespeist,
nicht aus dem Adress-ACK. Schlaegt `begin()` fehl, meldet der Node das im
Klartext samt Adresse, gibt den Hinweis auf `--bme on` / `--bmp on`, setzt
`bBME680ON = false` (Laufzeit, nicht gespeichert -- wie der bestehende
Konflikt-Zweig) und kehrt zurueck, bevor Oversampling, Filter und Gasheizer
konfiguriert werden. Die liefen bisher auch nach einem fehlgeschlagenen
`begin()`.

Auf Hardware verifiziert (Heltec V3, BME280 an 0x76):

vorher: "[INIT]...BME680 sensor found at 0x76", --wx zeigt "BME680: on (found)",
danach dauerhaft "Failed to complete reading :(" in jedem Zyklus
nachher: "[INIT]...BME680 an 0x76 antwortet, ist aber keiner (Chip-ID passt
nicht) - BME680 aus", --wx zeigt "BME680: off",
null "Failed to complete reading" im gesamten Mitschnitt

Der positive Pfad bleibt unberuehrt: "--bme on" auf demselben Board meldet
"BME280 startet" und liefert 25,9 Grad C, 37,2 %rH, 963,0 hPa.

### N-28 — `--help` verspricht `--bmx off` fuer BME680, der Handler kann das nicht — **FIXED (auf echter Hardware verifiziert)** — Low, UX

`command_functions.cpp:752` druckt `--bmx BME/BMP/680 off`. Der Handler
`:1894` behandelt `bmx off`, `bme off` und `bmp off` gemeinsam und loescht
`bBMPON`, `bBMEON`, `bBMP3ON` — **nicht** `bBME680ON`. Zum Abschalten des
BME680 gibt es einen eigenen Zweig `680 off` (`:1930`).

Praktische Folge, am 2026-08-22 auf dem Heltec V3 beobachtet: nach `--bmx off`
gefolgt von `--bme on` antwortet der Node
`BME680 and BMx280 can't be used together!` und aktiviert den BME280 nicht.
Der Benutzer hat exakt das getan, was die Hilfe sagt, und der Node bleibt ohne
Sensor.

Fix: `--bmx off` raeumt jetzt zusaetzlich `bBME680ON`, `bme680_found` und das
gespeicherte Bit `node_sset2 & 0x0004` ab -- damit stimmt die Hilfe, ohne dass
sie geaendert werden musste. `--bme off` und `--bmp off` teilen sich zwar den
Zweig, meinen aber weiterhin genau ihren eigenen Chip; nur das Sammelkommando
raeumt mit auf. Kollateralschaden gibt es keinen, weil BME680 und BMx280 sich
die Adressen teilen und ohnehin nie gleichzeitig aktiv sein koennen.

Auf Hardware verifiziert: nach `--680 on` (das wegen N-27 zur Laufzeit
scheitert, dessen gespeichertes Bit aber gesetzt bleibt) probiert der Node es
bei jedem Boot erneut. Nach `--bmx off` und Neustart erscheint keine einzige
BME680-Zeile mehr -- das gespeicherte Bit ist weg.

## 3. Refuted claims — do not re-investigate

| Claim                                                                                                                           | Refuting evidence                                                                                                                                                                                                                                                                                                                                                   |
| ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| The `(BOARD_E22_S3)` guard is a hard build error; two default envs do not compile                                               | `pio run -e E22_1262_S3-DevKitC-1-N16R8` → **SUCCESS** (93 s). PlatformIO strips the quotes; the macro reaches the compiler unquoted. See N-10.                                                                                                                                                                                                                     |
| `snprintf(value, 100, …)` on `char value[40]` (`web_functions.cpp:1660`) is an error-severity overflow                          | Source is `char node_mcp17t[16][16]` — max 16 bytes. Rule violation (bound must be `sizeof(dst)`), **not** a memory-safety bug.                                                                                                                                                                                                                                     |
| `snprintf(val, 160, …)` in `extudp_functions.cpp` overflows (cited `:195` pre-rebase; `val` is now declared at `:225`)          | `char val[160+1]`, and an explicit guard above caps dst ≤ 9 and msg ≤ 150. Safe.                                                                                                                                                                                                                                                                                    |
| `[nrf52]` triple declaration is a glob-order race                                                                               | ConfigParser merges option-by-option. `pio project config` output is deterministic. See C-05.                                                                                                                                                                                                                                                                       |
| `MAX_APRS_FRAME_SIZE 340` vs 255-byte parser buffers is exploitable                                                             | All six callers traced; none can supply more than 255 bytes today. **Latent**, not live.                                                                                                                                                                                                                                                                            |
| The nRF52 SX126x `"LORA"` task runs at priority 2 and preempts `loop()` (claimed by C-01 in the first version of this document) | `board.cpp:44` defines `TASK_PRIO_NORMAL` as the macro `1`; `#ifndef` cannot see the core's `= 2` enum. `board.cpp:498` therefore creates the task at priority 1, same as `loop_task` (`main.cpp:88`, `TASK_PRIO_LOW = 1`), and `configUSE_TIME_SLICING` is 0. The real priority-2 path is the FreeRTOS timer service task on a 1 KB stack. See the corrected C-01. |
| The concept's "218 commands"                                                                                                    | 217 arms / 227 verbs. See C-08.                                                                                                                                                                                                                                                                                                                                     |
| `-Wall -Wextra` produces an unmanageable warning backlog                                                                        | Measured: **9 warnings total**, 4 in `src/`. The June audit's `-Werror` exception rests on a backlog that does not exist.                                                                                                                                                                                                                                           |

---

## 4. What replaces the golden-vector plan

> **STATUS 2026-08-21 — erster Ausbau umgesetzt.**
>
> - **Mechanismus 2 (Capture-Pfad): DONE, seit 2026-08-25 zur Laufzeit
>   schaltbar.** `OnRxDone()` (`lora_functions.cpp`) und `doTX()` reichen die
>   Rohbytes an `captureFrame()` (`src/capture_functions.cpp`); ausgegeben wird
>   `[MC-TEST] RX_FRAME len rssi snr hex=…` bzw. `TX_FRAME len hex=…` aus dem
>   Loop-Kontext (`captureDrain()` in `main.cpp`). Gegenpol zum
>   `CRC_PAYLOAD`-Dump der verworfenen Frames, am Seam den nur akzeptierte
>   Frames erreichen (beide Plattformen).
>
>   Das frueher noetige Compile-Gate `-D MC_TEST_HOOKS` ist entfallen: nicht
>   der Dump war zu teuer, sondern sein Ort. Direkt im Radio-Callback braucht
>   `printfdeb()` ~900 Byte Stack (nRF52-Timer-Task hat 1 KB) und ~48 ms
>   Serial-Zeit im RX-Pfad; auf der TX-Seite saesse er zwischen CAD und
>   `startTransmit()`. Ueber einen SPSC-Ring entkoppelt kostet die Erfassung
>   nur ein `memcpy`. Preis: 768 B Ring (RAK4631 +1.400 B RAM, +1.616 B Flash).
>   `[MC-TEST] CAPTURE_DROPPED n=… serial_bytes=…` meldet BEIDE Verlustquellen:
>   Frames, fuer die im Ring kein Platz war, und Bytes, die `printfdeb()` auf
>   nRF52 verworfen hat, weil der USB-CDC-Puffer voll blieb. Die zweite Zahl
>   gehoert neben die erste — ein Frame kann sauber durch den Ring laufen und
>   trotzdem unvollstaendig im Log landen.
>
> - **Layer-B-Replay: DONE (2026-08-25).** `tools/traceharvest.py` erntet die
>   Entscheidungsfolge laufender Knoten aus der `[MC-DBG]`-Ausgabe; drei Suiten
>   fahren sie gegen den echten Code nach, nicht gegen eine Nachbildung:
>
>   | Suite                | Was nachgefahren wird                  | Ergebnis                                                             |
>   | -------------------- | -------------------------------------- | -------------------------------------------------------------------- |
>   | `test_dedup_replay`  | `is_new_packet()`, `addLoraRxBuffer()` | 5.647 Urteile + 6.869 Slotbelegungen, 0 Abweichungen                 |
>   | `test_txprio_replay` | `getMessagePriority()`                 | 505 Einstufungen ueber alle fuenf Prioritaetsklassen, 0 Abweichungen |
>   | `test_ack_replay`    | `isPlausibleAckFrame()`                | 30 im Feld honorierte ACKs, 0 wuerden verworfen                      |
>
>   Voraussetzung dafuer war die Herausloesung von `dedup_functions.{h,cpp}`
>   aus `lora_functions.cpp`/`loop_functions.cpp` (reine Verschiebung, wie
>   zuvor `txring_functions.cpp`) und von `ring_index.h` aus
>   `loop_functions_extern.h`. Alle drei Suiten sind mutationsgeprueft: eine
>   gezielte Aenderung am jeweiligen Code faerbt sie rot.
>
> - **Feldkorpus: DONE (2026-08-25).** `tools/logharvest.py` erntet
>   `test/test_aprs_fuzz/` (500 CRC-verworfene + 500 ACK-Frames, byte-exakt)
>   und `test/test_aprs_reencode/` (3.000 Re-Enkodier-Vektoren) aus
>   Produktionslogs. `[env:native_aprs_fuzz]` faehrt beides unter
>   ASan/UBSan.
> - **Native Decoder-Umgebung: DONE.** `[env:native_aprs]` kompiliert
>   `aprs_functions.cpp` (decode+encode) auf dem Host; noetig war nur ein
>   Minimal-Shim `test/support/nrf52/WisBlock-API.h` (die 9 von aprs_functions
>   gelesenen Settings-Felder) plus eine Handvoll Link-Stubs im Test. Damit ist
>   die Voraussetzung fuer Mechanismus 1 (differentielles Testen) geschaffen:
>   Prae- und Post-Fix-Decoder koennen jetzt beide nativ gebaut werden.
> - **Mechanismus 3 (Vektoren), erster Bestand:** `test/test_aprs_decode/` haelt
>   on-air mitgeschnittene Frames FREMDER Nodes als Interop-Vektoren — encodiert
>   von fremder Firmware, Sollwerte von Hand aus den Roh-Bytes gelesen (nicht aus
>   dem Pruefling). `pio test -e native_aprs`.
> - **Mechanismus 1: DONE (`a14eaada`, 2026-08-21 abends)** — als
>   Snapshot-Differential: `test_aprs_corpus` vergleicht die kanonische
>   decodeAPRS()-Ausgabe jedes Korpus-Frames gegen die eingecheckte
>   `golden.txt`; der git-Diff der Golden-Datei ist das Review-Artefakt.
>   Dazu Roundtrip-Pruefung decode→encode→decode je Frame.
> - **Korpus verbreitert: DONE** — 13 Frames, alle Haupttypen (Position auch
>   mit Track-Flag, Text, Gruppen, DM mit `{NNN`, Text-ACK, kompakter
>   12-Byte-Binaer-ACK `0x41` — Neufund: diese SIND on-air, Layout
>   `lora_functions.cpp:1078ff` —, HEY, 4-Hop-Pfade, Fremd-Encoder inkl.
>   MCProxy-BLE-Pfad und IV3OEP aus Italien).
> - **Wire-Format-Dokument: DONE** — `docs/architecture/11-wire-format.md`
>   (englisch): LoRa-Frame, Server-UDP (KEEP/DATA/GATE/CONF/BEAT), EXTUDP-JSON
>   und BLE-Phone-Protokoll, mit Byte-annotierten Real-Beispielen und
>   file:line-Ankern; Zweck: Mock-Services fuer mc-chat/MCProxy/mcmap.
> - **Mechanismus 3, Ausbaustufe (Spec-Vektoren): DONE (2026-08-21 spaet)** —
>   `test/test_aprs_spec/`: Frames Byte fuer Byte aus doc 11 konstruiert
>   (eigener Builder, eigene FCS-Summe, unabhaengig vom Encoder). Abgedeckt:
>   alle 16 Byte-5-Flag-Kombinationen, FCS-Regel, Gruppen-/Sonderziele,
>   Trailer-Optionalitaet, FW-Sub-Platzhalter, 0x41-Klassifikation VOR der
>   Mindestlaenge, elf Verwurfregeln, Encoder byte-genau. 13/13 gruen —
>   keine Abweichung Dokument<->Code gefunden.
> - **BLE-Kapitel-Vertiefung: DONE (2026-08-21 spaet)** — doc 11 §4 jetzt mit
>   Hello-Handshake inkl. PIN-Auth (SHA-256 ueber "%06u"-PIN,
>   `phone_commands.cpp:307ff`), Post-Hello-Config-Burst (`config_cmds[]` +
>   MHeard + CONFFIN-Reihenfolge) und allen 15 `0x44`-JSON-Schemata (TYP
>   I/SE/S1/SW/S2/SN/W/G/SA/IO/TM/AN/MH/CONFFIN). Korrektur dabei: MHeard
>   geht als `MH`-JSON zum Phone; der `0x91`-Binaer-Zweig in den Sendern hat
>   keinen Produzenten mehr (Legacy). Quirk dokumentiert: jede Notification
>   traegt 2 Bytes ueber die Nutzlaenge hinaus (`blelen + 2`).
> - **§2-Quervalidierung (Server-UDP): DONE (2026-08-21 nachts)** — doc 11 §2
>   gegen die zweite unabhaengige Implementierung `mc-chat/meshcom_mock/`
>   (Softnodes live am OE- und DL-Server) und die Upstream-Spezifikation
>   `icssw-org/MeshCom-Reflector` quergeprueft. Ergebnisse eingearbeitet:
>   msg_id-Komposition `(gw_id<<10)|counter` mit Wrap bei 1000 (drei
>   Clamp-Stellen `loop_functions.cpp:3131ff` — deshalb nicht 1024);
>   BEAT-Binnenstruktur (`BEAT+0x00+len+call[+0x01+len+status]`, Server
>   antwortet auf JEDES KEEP); DATA-Trailer "03" = ASCII-Modulationsziffern
>   (Upstream-Spec-Fehler "PAYLOAD_LEN" dokumentiert); **CONF ist
>   nRF52-only** (ESP32-getUDP hat keinen CONF-Zweig — Plattform-Divergenz
>   im DRY-21-Klon); Ack-Format exakt `%-9.9s:ack%03i`; `{NNN` ohne
>   schliessende Klammer (nur `{pong}{NNN}` mit); neues §1.7
>   Steuer-Payloads ({CET}/{SET}/{MCP}/{ping}); neues §5 INTERLINK-Abgrenzung
>   (Port 1985, Server-zu-Server-Feed, Firmware spricht es nicht; mcmap
>   konsumiert ausschliesslich darueber).
> - **Offen:** — (Orakel-Plan vollstaendig umgesetzt).

C-03 removed the oracle. The zero-tolerance requirement needs a real one. Three
mechanisms, in ascending cost:

1. **Differential testing against the current build.** Compile the _pre-fix_ and _post-fix_
   decoder into one native binary and assert they agree on a large corpus of generated and
   captured frames. This is a true before/after comparison and needs no external oracle —
   it directly encodes "we did not change behaviour we did not mean to change".
2. **A real capture path.** Dump **accepted** frames as raw bytes, not just the CRC-failed
   ones. Implemented 2026-08-25 as `src/capture_functions.cpp` (`--loradebug on` for RX,
   `--txcapture on` for TX), buffered through an SPSC ring so the copy stays out of the
   radio callback and out of the CAD-to-transmit window. That produces the corpus 06
   assumed already existed.
3. **Hand-authored specification vectors.** For the frame layouts documented in a future
   wire-format document, write vectors from the _specification_, not from the decoder's
   output. Only these can catch a pre-existing decode bug; the other two are regression
   fences.

`--inject` (07 H-01) remains the right hook, but at the wrong seam: `CRC_ERROR`,
`CRC_PAYLOAD` and `ERR_PAYLOAD` live in `checkRX` _above_ `OnRxDone`, so scenario 3 only
appears covered. Inject at the `checkRX` boundary instead.

Also corrected from 07 §7: scenario 20 is tautological (`--lora` reads back
`meshcom_settings`, never the SX1262); scenario 11 names the wrong marker
(`TX_GATE_ENTER` fires _after_ the backoff — use `RX_TIMEOUT_FIRE wait=`); scenario 12 is
unachievable (10 sites share the PRNG, so seeding fixes the sequence but not the
position). Of 25 scenarios: 12 falsifiable, 6 after correction, **7 cannot fail or cannot
be built**.

A native build additionally falls into the `#else` of `configuration_global.h:79-116` and
silently selects `MAX_MHEARD` 30 / `MAX_DEDUP_RING` 70 / `MAX_RING` 30 — against 80/100/20
on both bench boards. Every ring dimension differs. The native environment must pin an
explicit board profile.

---

## 5. Remediation order

Each row is one commit and one upstream PR. Upstream has merged 24 PRs from this author
(`+1` to `+3984` lines), so small surgical PRs are the proven path.

### Wave 0 — enablement (no behaviour change, no hardware)

| #       | Item                                                                                       | Evidence              | Status                                                              |
| ------- | ------------------------------------------------------------------------------------------ | --------------------- | ------------------------------------------------------------------- |
| 0.1     | CI: build all 32 envs on PR and push                                                       | `TEST-38`             | done — CI build gate                                                |
| ~~0.2~~ | `-Wall -Wextra` on firmware targets, fix the warnings, then `-Werror` on `build_src_flags` | F6, C-17              | **DONE (ESP32)** 2026-08-18 — see STATUS box below                  |
| ~~0.3~~ | `-Wundef` + convert `BOARD_*` to flags with separate name macros                           | N-10                  | **DONE** 2026-08-18 — see STATUS box on N-10 below                  |
| ~~0.4~~ | Pin `nordicnrf52`                                                                          | 02 B-04               | **DONE** 2026-08-20 — auf `10.12.0` gepinnt, alle drei nRF52-Boards |
| 0.5     | `[env:native]` + Unity + `Arduino.h` shim, explicit board profile                          | `TEST-37`, C-14, C-03 | done — native test harness                                          |

> **STATUS 2026-08-18 — 0.2 DONE fuer ESP32.**
>
> Ausgangslage war anders als hier notiert: `-Wall -Wextra` stand bereits im
> `[esp32]`-Block (`build_flags`), galt also laengst fuer alle ESP32-Targets — die
> Notiz "currently only safeboot" war veraltet. Was fehlte, war das Aufraeumen der
> Warnungen und das Scharfschalten.
>
> Gemessen auf `heltec_wifi_lora_32_V3` (Clean-Build): genau **3 Warnungen in `src/`** —
> 2x `-Wclobbered` in `src/Regexp.cpp:297` (`pattern`/`index` werden nach dem
> `setjmp()` noch veraendert; folgenlos, weil der Fehlerpfad beide nicht mehr liest,
> per `volatile` jetzt aber explizit garantiert) und 1x `-Wmisleading-indentation` in
> `src/net_console.cpp:287` (`s_listen_fd = -1;` stand ungeschuetzt hinter dem `if`;
> Verhalten war zufaellig korrekt, die Form ist es nicht). Alle uebrigen Warnungen
> stammen aus `.pio/libdeps` (TinyGPSPlus, BMx280MI, OneWire) und sind nicht unsere.
>
> Scharfgeschaltet wurde deshalb ueber `build_src_flags` (nur `src/`), nicht ueber
> `build_flags` — sonst brechen die Drittanbieter-Warnungen den Build:
> `-Wformat=2 -Wno-missing-field-initializers -Werror`.
>
> `-Wformat=2` ist bewusst dabei: es haette die SEC-02-Fundstelle sofort gemeldet.
> `-Wno-missing-field-initializers` (aus `-Wextra`) musste raus, weil es die
> idiomatische ESP-IDF-Strukturinitialisierung trifft (`i2s_driver_config_t` in
> `src/esp32/esp32_audio.cpp:540`, brach `t_deck`/`t_deck_plus`); die Restfelder sind
> IDF-versionsabhaengig und bewusst 0.
>
> **Reichweite — wichtig, weil kleiner als "alle ESP32-Targets":** die Flags haengen am
> `[esp32]`-Block und gelten damit exakt fuer die **23 Envs mit `extends = esp32`**.
> Alle 23 wurden gebaut, alle SUCCESS.
>
> Nicht abgedeckt, weil ohne `extends = esp32` (erben weder `-Wall -Wextra` noch
> `build_src_flags`, bauen unveraendert):
>
> - ESP32, aber eigener `build_flags`-Block: `t_deck_pro`, `t5_epaper`,
>   `vision-master-e213`, `vision-master-e290`, `wireless-paper` — hier fehlt schon
>   `-Wall -Wextra`, deshalb waere `-Werror` allein wirkungslos. Diese fuenf bleiben
>   Rest von 0.2.
> - nRF52: `wiscore_rak4631`, `heltec_t114`, `t_echo` — bewusst offen bis Hardware
>   angeschlossen ist, dann zusammen mit 0.4.
> - `esp32-safeboot`: vorbestehend defekt, von dieser Aenderung nachweislich nicht
>   betroffen (kein `extends = esp32`); scheitert an einem Toolchain-Fehler des
>   Tasmota-Platform-Forks (`FRAMEWORK_DIR` = None in `arduino.py`), nicht an einer
>   Warnung. `esp32-S3-safeboot` baut sauber. _(Am 2026-08-21 gefixt, `e0f28bef`:
>   Paket-Verzeichnis-Kollision mit der Mainline-Plattform, behoben per pre-Skript
>   `tools/ensure_tasmota_framework.py` in beiden Safeboot-Envs.)_
>
> Auf Hardware (Heltec V3) geflasht und geprueft: kein Boot-Loop, Webserver HTTP 200,
> BLE verbindet.

> **STATUS 2026-08-20 — 0.2 fuer `wiscore_rak4631` DONE, `heltec_t114`/`t_echo` deckten
> `CFG-01` auf (siehe eigener Befund unten).**
>
> Vor dem Gate 20 echte Warnungen in `src/` gemessen (nicht die ~33.000 `-Wunused-parameter`
> aus SVCALL-Makro-Expansionen in den Nordic-SoftDevice-Headern — Drittanbieter, via
> `-Wno-unused-parameter` in `build_src_flags` ausgeklammert, analog zum
> `-Wno-missing-field-initializers`-Muster oben). Alle 20 behoben (`(void)`-Casts bzw.
> `int`→`size_t`), `-Wformat=2 -Wno-unused-parameter -Werror` scharfgeschaltet. Details und
> Dateiliste: Commit `a3a30ef0`.
>
> Derselbe Versuch fuer `heltec_t114`/`t_echo` legte einen unabhaengigen Befund frei
> (`CFG-01`) und wurde deshalb fuer diese zwei Boards zurueckgestellt — nicht ohne
> angeschlossene Hardware sauber verifizierbar.

### Wave 1 — RF-reachable criticals (each a standalone PR)

| #       | Item                                   | ID              | Size        | Status                                                   |
| ------- | -------------------------------------- | --------------- | ----------- | -------------------------------------------------------- |
| 1.1     | `printfdeb` non-literal format string  | `SEC-02`        | 1 line      | **DONE** 2026-08-18                                      |
| 1.2     | CONF zero-fill overflow                | `N-03`          | delete loop | **DONE** 2026-08-18                                      |
| ~~1.3~~ | `{MCP}` password bypass                | `N-01`/`SEC-01` | small       | ACCEPTED / WONTFIX 2026-08-18 — maintainer decision      |
| ~~1.4~~ | `{SET}` unauthenticated routing change | `N-02`          | small       | ACCEPTED / WONTFIX 2026-08-18 — maintainer decision      |
| 1.5     | `memcpy` length underflow chain        | `N-04`/`BUG-08` | small       | **DONE** 2026-08-18                                      |
| 1.6     | mheard heap over-read                  | `N-05`          | 1 line      | **DONE** 2026-08-18                                      |
| 1.7     | web `t_io` bound check                 | `N-06`          | 2 lines     | **DONE** 2026-08-18                                      |
| ~~1.8~~ | BLE command gate                       | `N-07`          | small       | ACCEPTED / WONTFIX 2026-08-18 — bonding breaks app fleet |

**Wave 1 is closed** — all 8 items done or deliberately accepted as risk. See the Standing risk
box in `docs/BACKLOG.md` for what "done" means here (fixed locally, not yet upstream).

### Wave 2 — remaining prior-verdict Track A

**Done, 2026-08-18:** `N-08` (millis() rollover), `N-09` (corrected, no live hazard — see
STATUS box below), `SEC-03`, `SEC-04`, `SEC-05`, `SEC-06`, `BUG-07`, `BUG-10`, `BUG-11`,
`BUG-12`, `BUG-13`, `CONC-14`, `CONC-19`.

**Still open:** `N-14` — **auf nRF52**, siehe eigene STATUS-Box (Scope groesser als
urspruenglich katalogisiert, bewusst nicht in dieser Session angefasst). `CONC-15`/`16`/`18`,
`N-16` **BEHOBEN** 2026-08-20, `N-15` als bereits geschlossen re-verifiziert, `CONC-17`
**BEHOBEN** 2026-08-20 — siehe die jeweiligen Fundstellen oben und die STATUS-Box unten. Fuer
ESP32 einzeln nachgeprueft und geschlossen, siehe STATUS-Box.

> **STATUS 2026-08-18 — ESP32 einzeln nachgeprueft: kein offener Wave-2-Punkt mehr.**
>
> Der Katalog verlangte hier ausdruecklich, `CONC-15`/`16`/`17`/`18` **nicht** auf die
> Behauptung des Vorgutachtens hin abzuhaken ("resolved at the root by CONC-14"), sondern
> einzeln zu pruefen. Genau das ist jetzt geschehen — mit dem Ergebnis, dass auf ESP32
> keiner der vier ein Defekt ist, aber aus einem anderen Grund als behauptet.
>
> `CONC-14` war ein **nRF52**-Fix (`a441ece6`, `nrf52_ble.cpp`); er hat auf ESP32 gar nichts
> geaendert, weil ESP32 die Queue-Entkopplung schon immer hatte. Tragend ist stattdessen
> `C-01`: der gemeldete Mechanismus aller drei Ring-Findings lautet "Radio-Task rennt gegen
> Loop-Task" — diese Praemisse gilt auf ESP32 nicht.
>
> Nachgewiesen am Build `heltec_wifi_lora_32_V3`:
>
> - **Genau eine** zusaetzliche Task existiert in diesem Build: `authTask`
>   (`net_console.cpp:383`, Core 1, Prio 1). Sie fasst **keinen** Ring an und ruft kein
>   `commandAction` — nur HMAC-Handshake, dann `s_fd`/`s_authenticated` unter dem Mutex.
>   Die Tasks in `t-deck-pro/`, `t5-epaper/` und `esp32_audio.cpp` sind per `src_filter`
>   bzw. `ENABLE_AUDIO` nicht Teil dieses Builds.
> - `OnRxDone()` laeuft synchron in `loopTask`: `esp32loop()` → `checkRX()`
>   (`esp32_main.cpp:2272`) → `OnRxDone()` (`:3918`). Der Extern-Radio-Pfad liefert RX
>   ebenfalls synchron waehrend `poll()` (`external_radio_glue.cpp:164`), auch aus der Loop.
> - Die ESP32-ISRs `setFlagReceive`/`setFlagSent` (`esp32_main.cpp:490`, `:506`) fassen
>   ausschliesslich `receiveFlag`/`transmittedFlag` an (beide weiterhin `std::atomic<bool>`) —
>   keinen Ring-Index.
> - Die NimBLE-Host-Task erreicht die Ringe nicht: `CharacteristicCallbacks::onWrite` macht
>   nur `xQueueSend(bleQueue, …)`; die Server-Callbacks setzen nur Flags.
> - Der Webserver hat keine eigene Task: `CommonWebServer : public WiFiServer`
>   (`web_commonServer.h:18`), gepollt aus `loopWebserver()` (`esp32_main.cpp:3727`). Der
>   `toPhoneRead`-Zugriff in `web_functions.cpp:1264` liegt damit ebenfalls in `loopTask`.
>
> Damit liegen **alle** Schreiber und Leser von `toPhoneWrite`/`toPhoneRead`
> (`addBLEOutBuffer`, `sendToPhone`) und `udpWrite`/`udpRead` (`addUdpOutBuffer`,
> `sendMeshComUDP`) auf ESP32 in derselben Task. Kein Interleaving moeglich:
>
> - `CONC-15` (`toPhoneWrite/Read`) — **kein Defekt auf ESP32**
> - `CONC-16` (`udpWrite/Read`) — **kein Defekt auf ESP32**
> - `CONC-18` (`sendToPhone` TOCTOU) — **kein Defekt auf ESP32**
> - `CONC-17` — betrifft `nrf52_ble.cpp:319`, auf ESP32 gar nicht vorhanden
> - `N-14` (nRF52-TX-Ring), `N-16` (`taskENTER_CRITICAL` + `Radio.Send()`) — nRF52-only
> - `N-15` — sagt im eigenen Befundtext "True on ESP32"; der entfernte Guard ist auf ESP32
>   korrekt, der Defekt liegt auf nRF52
>
> **Bewusst kein Code geaendert.** Atomics oder Locks hier waeren genau die
> Ueber-Synchronisation, die `N-13` gerade entfernt hat.
>
> **Gueltigkeitsbedingung** (bei Verletzung neu bewerten): sobald auf ESP32 eine weitere
> Task eingefuehrt wird, die `addBLEOutBuffer`/`addUdpOutBuffer`/`sendToPhone` erreicht —
> etwa ein asynchroner Webserver, ein eigener Task fuer das Extern-Radio-Transport oder ein
> BLE-Callback, der `readPhoneCommand` wieder inline aufruft — fallen `CONC-15`/`16`/`18`
> auf ESP32 sofort zurueck auf "offen".
>
> Auf nRF52 bleiben alle sieben offen und unveraendert gueltig; dort ist `OnRxDone` ueber die
> FreeRTOS-Timer-Task (Prio 2) erreichbar. Abzuarbeiten, sobald ein nRF52 angeschlossen ist.

> **STATUS 2026-08-20 — nRF52-Durchgang: sechs von sieben behoben, `N-14` bewusst
> zurueckgestellt (Scope-Fund).**
>
> Mit angeschlossenem `wiscore_rak4631` einzeln abgearbeitet, gleiches Muster wie beim
> ESP32-Nachweis oben — kurzer `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` nur um den
> tatsaechlichen Ring-Zugriff, Debug-Ausgaben (koennen `malloc()`en,
> `printfdeb_functions.cpp:65) bewusst ausserhalb des Locks:
>
> - `CONC-17` (Commit `bb97b87c`) — `settings_rx_callback()` kopiert nicht mehr live in
>   `meshcom_settings`, sondern staged in einen privaten Puffer; `applyPendingBleSettings()`
>   wendet die Kopie einmal pro `nrf52loop()`-Durchlauf unter Lock an.
> - `CONC-15`/`CONC-18` (Commit `ed9116f6`) — `addBLEOutBuffer()` schreibt Ring-Slot und
>   Index-Vorschub jetzt unter Lock; `sendToPhone()` snapshot't Laenge/Status/Payload in
>   einem Rutsch statt spaeter erneut aus dem live Ring zu lesen (schliesst die TOCTOU).
>   `sendComToPhone()`/`ComToPhoneWrite`/`ComToPhoneRead` bewusst nicht angefasst: alle
>   Aufrufer von `addBLEComToOutBuffer()` laufen bereits im Main Loop (verifiziert), keine
>   Nebenlaeufigkeit vorhanden.
> - `CONC-16` (Commit `ca574ef7`) — dieselbe Behandlung fuer `udpWrite`/`udpRead`
>   (`addUdpOutBuffer()`/`sendMeshComUDP()`). Auf `wiscore_rak4631`/`heltec_t114` per
>   `--gc-sections` nicht gelinkt (Gateway/UDP-Pfad fuer diese Boards nicht erreichbar) —
>   nur auf ESP32 tatsaechlich verifizierbar gebaut, dort sind die neuen Locks No-ops. Auf
>   echter nRF52-Hardware mit Ethernet ungeprueft (kein Kabel am Bankarbeitsplatz, `DRY-21`).
>   **Korrektur 2026-08-21:** die "nicht gelinkt"-Aussage war falsch — der Schreiber
>   `addUdpOutBuffer()` laeuft via `addNodeData()` auch auf nRF52 (aus `OnRxDone`,
>   Timer-Service-Task), der nRF52-Leser ist `sendUDP()` in `nrf52_main.cpp` und las
>   ungeschuetzt. **Rest behoben** (`9117c3c6`): Snapshot unter Lock + Eviction-Guard,
>   auf dem RAK4631-Gateway mit Ethernet live verifiziert. Der im `ca574ef7`-Commit nur
>   dokumentierte **convBuffer-Ueberlesen-Nebenbefund ist ebenfalls behoben**
>   (`623c4c0e`, beide Fundstellen: APRS-Laenge `msg_len-36` statt Gesamtlaenge).
> - `N-16` (Commit `cc79611b`) — siehe eigene STATUS-Box oben.
> - `N-15` — kein Code-Fix noetig, bereits durch `CONC-14` geschlossen; siehe eigene
>   STATUS-Box oben.
>
> **`N-14` inzwischen GEFIXT (`efb2381b`, 2026-08-22) — siehe eigene STATUS-Box
> oben. Die folgende Scope-Analyse bleibt als Begruendung des gewaehlten
> Umbaus (kompletter Enqueue in einer Funktion statt Lock um `addTxRingEntry`)
> stehen:**
> Re-Verifikation zeigte: der TX-Ring-Schreibpfad ist NICHT in `addTxRingEntry()` gekapselt.
> Jeder der elf Aufrufer (`lora_functions.cpp:274,705,877,907,919,996,1000,1045,1139`;
> `loop_functions.cpp:642,3441,3458,3530,3975,4067` u.a.) schreibt selbst zuerst
> `ringBuffer[iWrite][0]=…`, `ringBuffer[iWrite][1]=…`, `memcpy(ringBuffer[iWrite]+2,…)`
> und ruft **danach** `addTxRingEntry()` auf, das `iWrite` **erneut** liest, statt den beim
> Aufrufer schon verwendeten Wert entgegenzunehmen. Ein Lock allein um
> `addTxRingEntry()` haette die eigentliche Rennbedingung (zwei Aufrufer schreiben in
> denselben, noch nicht fortgeschalteten Slot) nicht geschlossen — das Payload-Schreiben
> liegt ausserhalb der Funktion. Mindestens ein Aufrufer (`loop_functions.cpp:4198`,
> "beacon") liest `iWrite` zusaetzlich **vor** dem Aufruf in eine lokale Variable
> (`savedAckSlot`) und verwendet sie **nach** `addTxRingEntry()` fuer einen Nachtrag
> (`ringBuffer[savedAckSlot][1]=0xFF`) — ein korrekter Fix muesste diese gesamte
> Aufrufer-Sequenz pro Fundstelle unter denselben Lock nehmen, nicht nur die Funktion selbst.
> Ein sauberer Fix braucht vermutlich eine "Slot reservieren, dann schreiben"-Umkehr der
> Aufrufreihenfolge (`int slot = reserveTxSlot(); ringBuffer[slot][...] = ...;
finalizeTxRingEntry(slot, source);`), was **alle elf Aufrufer** aendert und die
> bestehende Prioritaets-Overflow-Logik (die aktuell VOR dem Fortschalten entscheidet, ob
> der neue Eintrag ueberhaupt angenommen wird) mit umbauen muss. Das ist eine eigene,
> sorgfaeltig zu verifizierende Session wert, keine kleine Ergaenzung zu den sechs Fixes
> oben. **Trigger fuer erneutes Aufgreifen:** dedizierte Session mit Hardware-Verifikation
> pro geaenderter Aufrufstelle.

### Wave 3 — structural (propose upstream as a plan first)

~~`N-13`~~ **DONE** 2026-08-18 — all 14 originally-identified over-synchronisation candidates
resolved across two commits (3 in the first pass, 4 more in a second pass), 2 confirmed as
not actual defects (`transmissionState`, the `pendingDisplay*` fields); see the STATUS box on
N-13 above. `iWrite`/`iRead`/`loraWrite` surfaced during re-verification as further same-class
candidates outside the original list — not part of N-13, deliberately left for a future pass.

**Offen:** `DRY-20`, `DRY-23`, `DRY-24`, `SIMP-26`, `SIMP-27`, `ALT-31`, `ALT-32`,
`STATE-28` (Epic-Teil), plus the corrected C-02 extraction of ~221 radio-independent shared
loop lines. ~~`DRY-22`~~ **BEHOBEN** 2026-08-20 (Commit `9b6c5224`) — siehe STATUS-Box.
`DRY-21`: die **Verhaltens-Drift ist behoben** (`07d1360f`, 2026-08-21 — ACK-Level 0x02
fuer eigene Nachrichten aus der ESP32-Kopie portiert, Debug-Zeile angeglichen); die
Zusammenlegung von `nrf_eth.cpp` und `udp_functions.cpp` bleibt als Upstream-Epic offen.

**Erledigt 2026-08-19 (ESP32/Heltec V3):** ~~`DRY-25`~~, ~~`SIMP-29`~~, ~~`SIMP-30`~~,
~~`ALT-33`~~, ~~`ALT-34`~~, ~~`ALT-35`~~ — siehe STATUS-Box unten.

> **STATUS 2026-08-19 — Track-B-Durchgang fuer ESP32/Heltec V3.**
>
> Abgearbeitet, jeweils ein Commit, jeweils gebaut und auf echter Hardware geflasht
> und geprueft (kein Boot-Loop, Webserver HTTP 200, BLE verbindet):
>
> | ID                           | Ergebnis                                                                                                                                                                                                                      |
> | ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
> | `SIMP-29`                    | **DONE** — `src/idf_component.yml.orig` (byte-identische Dublette) und `src/code_review/` (Audit-Bericht im kompilierten Quellbaum) entfernt                                                                                  |
> | `SIMP-30`                    | **DONE** — doppeltes `extern strSOFTSERAPP_ID` entfernt; HDOP-Zwillinge auf `fposinfo_hdop` zusammengelegt. Das war ein **echter Defekt**: mehrere Pfade setzten nur den int, Display und Webseite lasen verschiedene Quellen |
> | `DRY-25`                     | **DONE** — I2C-Bus-Reset-Guard zentral als `MC_I2C_NEEDS_BUS_RESET`. Echter Defekt: `bmx280.cpp:131/143` liessen `BOARD_E22_S3` aus, genau an den zwei Stellen, die den Sensor adressieren                                    |
> | `ALT-33`                     | **DONE** — die zwei byte-identischen Zweige (`ENABLE_XML`/`ENABLE_SBUFFER`) zusammengelegt                                                                                                                                    |
> | `ALT-34`                     | **DONE** — `DEFAULT_CALL`/`isNodeUnconfigured()` einmal in `configuration_global.h`, benutzt von allen drei Images                                                                                                            |
> | `ALT-35`                     | **DONE** — `bDisplayDirty` von `bOneButton` getrennt; das Flag bedeutet wieder ausschliesslich "Taste gedrueckt"                                                                                                              |
> | `iWrite`/`iRead`/`loraWrite` | **DONE** — N-13-Klasse, auf ESP32 ueber `ring_index_t` entatomisiert; Ring unter Last geprueft (`--sendpos`, drei `RADIO_TX`)                                                                                                 |
>
> **`STATE-28` — das gemeldete Live-Beispiel ist REFUTED.** Der Befund nennt
> `esp32_main.cpp` als Beleg: `bGATEWAY=false` beim Boot ohne Loeschen des
> persistierten `0x1000`-Bits, angeblich "`--info`/JSON-Export widersprechen dem
> Flash". Nachgeprueft: **beide** Ausgaben lesen den Bool (`command_functions.cpp:4996`
> und `:5368`), es gibt also gar keine widerspruechliche Ausgabe. Bleibt der Effekt,
> dass das Gateway beim naechsten Boot mit vorhandenen Zugangsdaten wieder angeht --
> das ist absichtserhaltend und gewollt: der Benutzerwunsch "Gateway an" ueberlebt
> eine Phase ohne WLAN. Das Bit zu loeschen waere die schlechtere Variante, weil der
> Benutzer das Gateway nach dem Nachtragen der Zugangsdaten neu einschalten muesste.
> Der uebrige Befund (zwei Wahrheitsquellen, ~94 Update-Stellen) bleibt als Epic offen.
>
> **Bewusst nicht angefasst — Epics, die der Katalog selbst "propose to upstream first"
> nennt und die gegen die Projektregel "minimal changes, kein Refactoring grosser
> Teile" laufen:** `SIMP-26` (commandAction, ~4860 Zeilen), `SIMP-27`
> (loop_functions.cpp aufteilen), `DRY-20` (zwei Batterie-Implementierungen),
> `DRY-24` (59 Toggles), `ALT-31` (Retransmit-Statusbyte), `ALT-32` (Display-Capability-
> Makros), `DRY-23` (enqueueTx/nextMsgId). `DRY-21`/`DRY-22` sind nRF52-seitig.
>
> **`BUG-09` ist bereits behoben** — der fehlende Clamp in `addBLEComToOutBuffer` kam
> mit `4e5ef591` (N-04) mit; `loop_functions.cpp:593` klemmt auf 245, Laengenbyte und
> `memcpy` sind seitdem konsistent. Nie als geschlossen vermerkt, hiermit nachgeholt.

> **STATUS 2026-08-20 — `DRY-22` BEHOBEN** (Commit `9b6c5224`). `checkSerialCommand()`
> existiert doppelt (ESP32/nRF52) und war auseinandergelaufen: die ESP32-Fassung hatte
> zwei Fixes, die nie nach nRF52 portiert wurden — NUL-Byte-Drop beim Lesen (UART-Rauschen
> liefert 0x00, das `strlen()` nicht sieht und den Parser haengen laesst) und eine
> Self-Healing-Invarianzpruefung (`strlen(strText) == iTxtPos`, sonst blockiert ein
> verirrtes NUL-Byte die Kommandoverarbeitung fuer immer). Beide 1:1 nach
> `nrf52_main.cpp:checkSerialCommand()` portiert. Der `#ifndef DISABLE_NET_CONSOLE`-Block
> (Telnet/Netzkonsole) bleibt bewusst aussen vor — alle drei nRF52-Boards definieren
> `DISABLE_NET_CONSOLE`, keine Drift dort. `wiscore_rak4631` gebaut, RAM unveraendert,
> Flash +32 B.

### Deferred, with triggers

| Item                                                      | Trigger to revisit                                                                                            |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Arduino 3.x migration                                     | after Wave 0 gives a RAM baseline and a CI gate                                                               |
| Arduino 2.0.14 → 2.0.17 on the four lagging boards (C-04) | with Wave 0's CI matrix in place                                                                              |
| LVGL 8 → 9                                                | never, unless the T-Deck UI is rewritten for other reasons                                                    |
| Radio interface / HAL                                     | only after C-02's cheap extraction proves the seam                                                            |
| ~~Licensing (N-11)~~                                      | **CLOSED** 2026-08-18 — risk accepted, maintainer decision                                                    |
| `FLASH_VERSION` migration (N-12)                          | re-verified 2026-08-18, still deferred — see STATUS box above; before any change to `meshcom_settings` layout |
| `iWrite`/`iRead`/`loraWrite` ring-buffer indices          | same-class N-13 candidates found during re-verification, not yet scoped                                       |
