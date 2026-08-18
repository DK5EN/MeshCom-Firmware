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

`fable-verdict.md` (repo root, 2026-07-12) already holds **39 verified findings**
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
> **Restbefund (neu, nicht behoben):** `src/phone_commands.cpp` liest dasselbe Laengenbyte
> in `sendToPhone()` (`:72`, `:84`) und `sendComToPhone()` (`:145`, `:151`) und rechnet
> `blelen-1` bzw. `blelen+2` ohne eigene Pruefung. Der Produzenten-Clamp schliesst den ueber
> HF erreichbaren Pfad, aber diese Konsumenten haben keine unabhaengige Absicherung. Kleiner
> Folge-Fix, bewusst nicht in denselben Commit gezogen, um den Upstream-Diff schmal zu halten.

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

### N-12 — `FLASH_VERSION` neither migrates nor resets — **CONFIRMED** — High

The version check runs _after_ `init_flash()` and is not followed by a re-read, so the old
RAM copy is written straight back — while the log prints `FLASH cleared new version`.
There are two incompatible `meshcom_settings` layouts (2008 B ESP32 / 1968 B nRF52); nRF52
raw-`memcpy`s its struct with two marker bytes as the only integrity check, so inserting a
field mid-struct corrupts every nRF52 node in the field. `--cleanflash` is a no-op on
nRF52.

This is the highest-risk item for any fleet-wide settings change.

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
> **Restbefund (bewusst nicht angefasst):** die uebrigen ~10 Kandidaten
> (`is_receiving`, `ch_util_tx_start`, `ch_util_rx_accum`, `ch_util_tx_accum`,
> `transmissionState`, die `pendingDisplay*`-Felder) sind dieselbe Kategorie, aber
> jeweils eigene Pruefung wert — dieser Umbau blieb auf die drei am eindeutigsten
> toten bzw. unnoetig gesperrten Stellen begrenzt, um den Diff schmal zu halten.
> Vier Boards sauber gebaut (`heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `t_deck`,
> `t_deck_pro`); ESP32-Boards RAM -16 Byte / Flash -108..-120 Byte, `wiscore_rak4631`
> unveraendert (nRF52-Pfad nicht beruehrt).

### N-14 — nRF52 TX ring is multi-writer with no mutual exclusion — **CONFIRMED** — High

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

### N-16 — blocking work inside critical sections on nRF52 — **CONFIRMED** — High

`Radio.Send()` inside `taskENTER_CRITICAL()` (`lora_functions.cpp:1685`, `:1726`, `:1787`)
reaches `SX126xWaitOnBusy()` → `delay(1)` → `vTaskDelay()` **with the tick frozen**.

---

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

C-03 removed the oracle. The zero-tolerance requirement needs a real one. Three
mechanisms, in ascending cost:

1. **Differential testing against the current build.** Compile the _pre-fix_ and _post-fix_
   decoder into one native binary and assert they agree on a large corpus of generated and
   captured frames. This is a true before/after comparison and needs no external oracle —
   it directly encodes "we did not change behaviour we did not mean to change".
2. **A real capture path.** Add a hex dump of **accepted** frames (currently only CRC-failed
   frames are dumped) behind `MC_TEST_HOOKS`, then re-capture on the bench. That produces
   the corpus 06 assumed already existed. Cheap — one `printfdeb` next to the existing one.
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

| #       | Item                                                                                                                   | Evidence              | Status                                             |
| ------- | ---------------------------------------------------------------------------------------------------------------------- | --------------------- | -------------------------------------------------- |
| 0.1     | CI: build all 32 envs on PR and push                                                                                   | `TEST-38`             | done — CI build gate                               |
| 0.2     | `-Wall -Wextra` on firmware targets (currently only safeboot), fix the 4 warnings, then `-Werror` on `build_src_flags` | F6, C-17              | open                                               |
| ~~0.3~~ | `-Wundef` + convert `BOARD_*` to flags with separate name macros                                                       | N-10                  | **DONE** 2026-08-18 — see STATUS box on N-10 below |
| 0.4     | Pin `nordicnrf52`                                                                                                      | 02 B-04               | open                                               |
| 0.5     | `[env:native]` + Unity + `Arduino.h` shim, explicit board profile                                                      | `TEST-37`, C-14, C-03 | done — native test harness                         |

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
box in `resume.md` for what "done" means here (fixed locally, not yet upstream).

### Wave 2 — remaining prior-verdict Track A

**Done, 2026-08-18:** `N-08` (millis() rollover), `N-09` (corrected, no live hazard — see
STATUS box below), `SEC-03`, `SEC-04`, `SEC-05`, `SEC-06`, `BUG-07`, `BUG-10`, `BUG-12`,
`BUG-13`, `CONC-14`.

**Still open:** `BUG-11`, `CONC-15` … `CONC-19`, `N-14`, `N-15`, `N-16`. `CONC-14`'s fix is
claimed by the prior verdict to resolve `CONC-15`/`16`/`17`/`18` at the root, but that was
**not re-verified** when `CONC-14` was fixed — treat all four as open until checked
individually.

### Wave 3 — structural (propose upstream as a plan first)

~~`N-13`~~ **PARTIALLY DONE** 2026-08-18 — 3 of 14 over-synchronisations removed (`scanFlag`
deleted, `displayMux` dropped on ESP32, `ch_util_rx_start` platform-split); see the STATUS box
on N-13 above for the ~10 deliberately deferred candidates.

`DRY-20` … `DRY-25`, `SIMP-26` … `SIMP-30`, `ALT-31` … `ALT-35`, `STATE-28`, plus the
corrected C-02 extraction of ~221 radio-independent shared loop lines.

### Deferred, with triggers

| Item                                                      | Trigger to revisit                                         |
| --------------------------------------------------------- | ---------------------------------------------------------- |
| Arduino 3.x migration                                     | after Wave 0 gives a RAM baseline and a CI gate            |
| Arduino 2.0.14 → 2.0.17 on the four lagging boards (C-04) | with Wave 0's CI matrix in place                           |
| LVGL 8 → 9                                                | never, unless the T-Deck UI is rewritten for other reasons |
| Radio interface / HAL                                     | only after C-02's cheap extraction proves the seam         |
| ~~Licensing (N-11)~~                                      | **CLOSED** 2026-08-18 — risk accepted, maintainer decision |
| `FLASH_VERSION` migration (N-12)                          | before any change to `meshcom_settings` layout             |
