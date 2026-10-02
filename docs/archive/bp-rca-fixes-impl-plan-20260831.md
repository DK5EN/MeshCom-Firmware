# Plan: Backpressure-RCA-Fixes BP-02 / BP-03 / BP-04 (v2, nach Advisor-Review)

**Status:** REWORK des Advisors (Fable, unabhaengig) eingearbeitet — 13 Findings,
davon F1 critical, F2/F3 high vom Orchestrator an den zitierten Zeilen
selbst verifiziert. Verdikt:
`plan-bp-rca-fixes-advisor-verdict.md` im selben Verzeichnis.
**Freigegeben durch den Operator 2026-08-31 (Umsetzung via orchestrate-waves).**

## Wave-Status

- Wave 1 (BP-02): DONE, Commit e501e63c (Advisor: 1 Rework RING_TX_READ, behoben; Gate 447/447 + 2 Builds)
- Wave 2 (BP-03): DONE, Commit 180917a1 (Advisor: APPROVED ohne Rework; Gate 327/327 + 2 Builds)
- Wave 3 (BP-04): DONE, Commit fdda7f2a (Advisor: APPROVED, 2 kosmetische Anmerkungen, A2 mitbehoben)
- Final: 12-Env-Native-Gate + 32-Env-Build: siehe RESUME-Eintrag 2026-08-31 (Nacht)

**ARCHIVIERT 2026-08-31: alle drei Fixes umgesetzt (e501e63c, 180917a1, fdda7f2a), jede Welle Fable-Advisor-gegatet.**

Herkunft: DJ8MEH-RCA 2026-08-31. Die QRT-Abweisung war korrekt, aber die
Episode lief 8 min auf Phantomtiefe: `txRingDepth()` = Indexdistanz
`iWrite - iRead` zaehlt freigegebene Loecher hinter einem
prioritaets-ausgehungerten Prio-5-Eintrag am Lesezeiger; QRV schliesst erst
bei Tiefe 0. Belegt: `RING_STATUS queued=19` bei real 3-4 belegten Slots.

Drei unabhaengige Fixes, je ein Commit (BP-02 -> BP-03 -> BP-04), alle in
host-testbarem Code.

---

## BP-02 — Ehrliche Ringtiefe: belegte Slots statt Indexdistanz

**`txRingDepth()`** (src/txring_functions.cpp:233): Schleife iRead->iWrite
(mit Wrap), zaehlt `ringBuffer[i][0] > 0`. Lock-frei wie heute; Staleness
gegenueber nebenlaeufigen Schreibern real bis ±2 Eintraege (F13) — fuer eine
Notice-Schwelle irrelevant, Kommentar sagt das ehrlich.

**Vereinheitlichte Berichtsgroessen** (alle auf Belegt-Zaehlung):

- `addTxRingEntry()`-Lokale `queued` (txring:339, unter Lock; speist
  RING_WRITE-Log und stat_queue_hwm). **F4:** `stat_queue_hwm` wird als
  "belegte Slots NACH dem Einfuegen" definiert und OHNE das bisherige `+1`
  fortgeschrieben (Zaehlung nach dem Slot-Write; nie > MAX_RING);
  `test_txring_flood.cpp:270` wird auf diese Semantik angepasst.
- `RING_STATUS` (esp32_main.cpp ~2068, nrf52_main.cpp ~1308):
  `queued = pending+retrying+done` (belegt). **F3:** zusaetzlich neues Feld
  `dist=%d` (Indexdistanz) ANS ENDE der Zeile — die bestehenden
  Parser-Regexe (tools/serial_monitor.py:47, tools/loganalyse.sh) matchen
  weiter; die RING_ZOMBIE-Detektoren in beiden Tools (serial_monitor.py:116/385,
  loganalyse.sh ~1693: `retrying>0 && queued==0`) werden im selben Commit
  auf `dist` umgestellt, sonst sterben sie stumm.
- `TX_GATE_ENTER qlen` (esp32_main.cpp ~2467) **und** `TX_START qlen`
  (esp32_main.cpp ~2561, F10) sowie die nrf52-Pendants: auf `txRingDepth()`.

**Nicht anfassen:** Slot-Vergabe/Voll-Erkennung in `addTxRingEntry()`
bleibt Index-Logik.

## BP-03 — Alterung von Background-Eintraegen (NICHT in getNextTxSlot)

**F1 (critical, verifiziert):** `getNextTxSlot()` laeuft auch im
nRF52-Timer-Task (`OnRxDone` -> `csma_compute_timeout()`,
lora_functions.cpp:499/1402 -> :2210) und im EXTERNAL_RADIO-Pfad. Dort darf
weder der Ring beschrieben noch geprintft werden. **Die Alterung wandert
daher in eine eigene Funktion `txRingAgeBackground(uint32_t now_ms)` in
`src/txring_functions.cpp`** (damit nativ testbar), aufgerufen aus dem
bestehenden 2-Sekunden-Tick neben `updateRetransmissionStatus()`
(esp32_main.cpp ~2045, nrf52-Pendant) — Main-Loop auf beiden Plattformen,
derselbe Kontext, der heute schon Slots freigibt.

**Sweep-Logik:** fuer jeden Slot: wenn `ringBuffer[i][0] > 0` UND
`ringPriority[i] == MSG_PRIO_BACKGROUND` (5, nur HEY/0x40) UND
`(uint32_t)(now_ms - ringEnqueueTime[i]) > RING_BG_MAX_AGE_MS` (F8:
Rollover-Cast) UND — bei EXTERNAL_RADIO — Status nicht
`RING_STATUS_EXT_PENDING` (F7, Owned-Slot-Invariante): Slot freigeben
(`len=0`, `retryCount=0`), `stat_drop_count[5]++`, Marker
`[MC-DBG] RING_DROP_STALE slot=%d age_s=%lu msg_id=%08X`. Nach dem Sweep
einmal `advanceIReadPastEmpty()` (F6), damit der Lesezeiger die Front
raeumt. Auf nRF52 Freigabe unter derselben kritischen Sektion wie in
`addTxRingEntry()` (F2: die Overflow-Eviction RELOZIERT den iRead-Eintrag
per memcpy in den geraeumten Slot — ohne Lock koennte der Sweep eine gerade
relozierte Nachricht loeschen; unter Lock wird der Slot-Zustand nach
Lock-Erwerb re-validiert).

**Konstante:** `#define RING_BG_MAX_AGE_MS 180000UL` (3 min,
configuration_global.h). Tradeoff dokumentiert (F11): auch ein EIGENER
HEY-Beacon altert; bei Trickle-Intervallen bis 480 s kommt die naechste
Kopie ggf. Minuten spaeter — akzeptiert, ein 3 min nicht sendbarer
Nachbarschaftsreport ist funklich wertlos, und der DJ8MEH-Blocker sass
10 min.

## BP-04 — QRV im Wasserband mit Zeit-Hysterese

`src/backpressure.h` + Aufrufer (`bpPollDrain()`, `sendMessage()`-Pfad in
loop_functions.cpp):

- Tiefe 0: schliesst sofort (heutiges Verhalten).
- Tiefe <= `QUIET_DEPTH` (1): schliesst erst nach `QRV_HOLD_MS = 10000`
  ununterbrochen (Advisor: 5 s vertretbar, 10 s robuster — 10 s gewaehlt;
  gegen eine minutenlange Episode ist das vernachlaessigbar). Jede
  Beobachtung > QUIET_DEPTH setzt die Haltezeit zurueck.
- API: `poll(int depth)` -> `poll(int depth, uint32_t now_ms)`,
  `onSend(depth, dropped)` -> `onSend(depth, dropped, uint32_t now_ms)`;
  Header bleibt Arduino-frei, Zeit injiziert, Rollover-sicher via
  `(uint32_t)(now - since)`.
- **F12:** `reset()`/`configure()` loeschen auch den Hold-Zustand
  (`quiet_since` Sentinel).
- Anti-Flap 08.30 (2 -> 1 -> 2 in 400 ms) bleibt abgedeckt: Tiefe 1 muss
  10 s stehen. Wird als Regressionstest festgenagelt.

## Tests (jeder Fix fails-before)

`test/test_txring` (native_aprs):

1. Tiefe-mit-Loechern: N Eintraege, mittlere freigeben ->
   `txRingDepth()` == Belegt-Zahl (vorher rot: Indexdistanz).
2. DJ8MEH-Szenario: Prio-5 am Lesezeiger, Loecher dahinter -> Tiefe klein
   (BP-02); `mc_test_set_millis` ueber die Grenze ->
   `txRingAgeBackground()` gibt frei, `advanceIReadPastEmpty()` rueckt vor
   (BP-03; Test ruft den Sweep, nicht getNextTxSlot — F6).
3. Sweep-Einzelfaelle: frischer BG bleibt; Nicht-BG altert nie;
   EXT_PENDING altert nie (EXTERNAL_RADIO-Guard, soweit im Env baubar);
   `stat_drop_count[5]` genau +1; Rollover-Fall (now < enqueue nach Wrap).
4. `stat_queue_hwm`: erreicht MAX_RING, nie MAX_RING+1 (F4;
   test_txring_flood.cpp:270 angepasst).

`test/test_backpressure` (native):

5. Wasserband: `poll(1, t)` -> kein QRV vor t+10 s, QRV genau einmal
   danach; Tiefe-2-Zwischenbeobachtung setzt zurueck.
6. Anti-Flap-Regression: 2 -> 1 (400 ms) -> 2 erzeugt kein QRV.
7. Bestand: Tiefe 0 schliesst sofort; `reset()` loescht Hold-Zustand (F12).

**F5:** `test_txring_flood` ruft `bp.poll`/`bp.onSend` und pinnt
`txRingDepth` — wird im jeweiligen Commit mit angepasst (Signaturen,
Tiefen-Erwartungen).

**Tooling im selben Zug (F3):** serial_monitor.py und loganalyse.sh:
RING_STATUS-Regex um `dist` erweitern, Zombie-Bedingung auf
`retrying>0 && dist==0`.

**Gates:** komplette Native-Suite (12 Envs), danach 32-Env-Build.
**Bench (optional, nach Merge):** DJ8MEH-Burst auf DK5EN-98; erwartet
QRT-Ende ~ realer Drain, `queued == pending+retrying+done`, QRV im
Minutenfenster.

## Nicht-Ziele / Risiken

- Kein Redesign der Prio-Auswahl, kein Retransmit-Umbau; TM-49
  zurueckgestellt; FLASH_STRUCT unberuehrt.
- Log-Semantik aendert sich (`queued` ehrlich, neues `dist`): ein Satz ins
  CHANGELOG; alte Logs bleiben nach alter Lesart auswertbar.
- On-Air: BP-03 verwirft veraltete HEY-Kopien (>3 min) — fuer "What changes
  on the air" des naechsten Release.
- Advisor-Findings F9 (continue-Endlosschleife) entfaellt durch die
  Verlegung des Sweeps; F1-F13 vollstaendig adressiert, keine abgelehnt.
