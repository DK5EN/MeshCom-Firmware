# Advisor-Verdikt: Plan BP-02 / BP-03 / BP-04

Review-Basis: Plan `plan-bp-rca-fixes.md` gegen den Code auf Branch
`tdeck-partial-refresh-trace` (Stand 2026-08-31). Alle Zeilenangaben aus dem
echten Code, selbst gelesen.

---

## Findings

### F1 — getNextTxSlot() laeuft auch im nRF52-Timer-Task: BP-03-Drop dort ist kein "Leser-Kontext"

**Datei:** src/lora_functions.cpp:499, 1402, 2210, 2244; src/txring_functions.cpp:151
**Severity:** critical

Der Plan platziert den Stale-Drop (Schreibzugriff `ringBuffer[pos][0]=0` +
Marker) in `getNextTxSlot()` und begruendet die Sicherheit damit, der Pfad
laufe "auf ESP32 nur im Main-Loop" und auf nRF52 im Leser-Kontext. Das
Aufrufer-Bild ist aber breiter:

- `csma_compute_timeout()` (lora_functions.cpp:2208) ruft `getNextTxSlot()`
  auf, um die Prioritaet des naechsten Pakets fuer den CSMA-Backoff zu
  bestimmen — und wird aus `OnRxDone()` heraus aufgerufen (Zeilen 499 und 1402) sowie aus `csma_reset()` (2244, u.a. OnTxDone-Pfad). Auf RAK4630
  laufen diese Radio-Callbacks im FreeRTOS-Timer-Service-Task, nicht im
  Main-Loop.
- Auf EXTERNAL_RADIO-Builds ruft zusaetzlich `externalTxMarkPendingNext()`
  (lora_functions.cpp:2077) `getNextTxSlot()` auf.

Fehlerszenario: OnRxDone (Timer-Task) berechnet den CSMA-Backoff, faehrt
dabei den BP-03-Drop und schreibt `len=0` in einen Slot, waehrend der
Main-Loop in `doTX()` genau diesen Slot zwischen `getNextTxSlot()` und
`sendlng = ringBuffer[txSlot][0]` (lora_functions.cpp:1527) liest —
`sendlng==0`, leerer Sendevorgang bzw. inkonsistenter Slot-Zustand.
Zusaetzlich druckt der geplante Marker dann aus dem Timer-Task
(printf-malloc-starves-nimble: BLE-Abriss-Risiko), und
`stat_drop_count[5]++` sowie ein etwaiges `advanceIReadPastEmpty()` wuerden
zu zweiten Schreibern ohne Lock.

**Plan-Korrektur:** Aging NICHT in `getNextTxSlot()` (das ist eine
Mehrfach-Kontext-Query-Funktion), sondern in den 2-s-Retransmit-Tick
`updateRetransmissionStatus()` (lora_functions.cpp:1803) legen: laeuft auf
beiden Plattformen nur im Main-Loop (esp32_main.cpp:2045,
nrf52_main.cpp:1284), scannt ohnehin alle Slots, und der Marker kann dort
regulaer nach der kritischen Sektion gedruckt werden. Alternativ ein
expliziter Parameter `bool allow_stale_drop` mit true nur am doTX-Aufruf —
aber der Retransmit-Tick ist die sauberere Stelle.

### F2 — "beide schreiben len=0, idempotent" ist widerlegt: die Overflow-Eviction ZIEHT UM, sie nullt nicht nur

**Datei:** src/txring_functions.cpp:403-410
**Severity:** high

Der Plan behauptet, die einzige Nebenlaeufigkeit der BP-03-Freigabe sei die
Overflow-Eviction und beide Seiten schrieben nur `len=0`. Tatsaechlich
relociert die N-24-Logik nach der Eviction den Eintrag von `iRead` in den
soeben geraeumten Slot (memcpy Payload + ringPriority + ringEnqueueTime +
retryCount) und leert stattdessen den iRead-Slot.

Fehlerszenario (nRF52, Plan wie geschrieben): Main-Loop prueft Slot X
(Prio 5, alt) und beschliesst den Drop; bevor die kritische Sektion greift,
evictet der Timer-Task in `addTxRingEntry()` genau X als worst_slot und
zieht den iRead-Eintrag — moeglicherweise eine CRITICAL-DM — nach X um; der
Main-Loop schreibt anschliessend `ringBuffer[X][0]=0` und loescht die DM
stumm und unbilanziert. Eine Freigabe "in derselben kritischen Sektion"
hilft nur, wenn die ENTSCHEIDUNG (len>0, prio==5, Alter) innerhalb derselben
Sektion re-validiert wird; der Plan spezifiziert nur den Schreibzugriff im
Lock (Check-then-act-Luecke).

**Plan-Korrektur:** Drop-Bedingung und Freigabe atomar: auf nRF52 innerhalb
`taskENTER_CRITICAL()` erneut `ringBuffer[pos][0]>0 && ringPriority[pos]==
MSG_PRIO_BACKGROUND && age>LIMIT` pruefen, erst dann nullen. Mit F1
(Verlagerung in den Main-Loop-Tick) schrumpft das Fenster, verschwindet auf
nRF52 aber nicht — der Timer-Task-Enqueue bleibt nebenlaeufig, die
Re-Validierung im Lock ist Pflicht.

### F3 — BP-02 macht die RING_ZOMBIE-Erkennung unerfuellbar

**Datei:** tools/serial_monitor.py:116, 177, 385; tools/loganalyse.sh:1693
**Severity:** high

Beide Auswerter definieren RING_ZOMBIE als `retrying>0 && queued==0` ueber
mehrere aufeinanderfolgende RING_STATUS-Zeilen. Das funktioniert heute genau
deshalb, weil `queued` die Indexdistanz ist und `retrying` ALLE Slots
(0..MAX_RING, auch ausserhalb des iRead/iWrite-Fensters) zaehlt
(esp32_main.cpp:2059-2068, nrf52_main.cpp:1297-1307) — der Zombie ist per
Definition ein belegter Slot ausserhalb des Fensters. Ersetzt BP-02 `queued`
durch `pending+retrying+done`, gilt kuenftig immer `queued >= retrying`; die
Bedingung `retrying>0 && queued==0` wird unerfuellbar und beide Detektoren
sterben stumm. Nebenbefund: die Plan-Behauptung "die Summe IST die
Belegt-Zaehlung" stimmt nur fensterweise — die Summe zaehlt Orphan-Slots
ausserhalb des Fensters mit, `txRingDepth()` (auch neu) nicht; die beiden
"ehrlichen Tiefen" koennen also im Zombie-Fall weiterhin divergieren.

**Plan-Korrektur:** Detektoren im selben Commit mitziehen: Zombie neu als
`retrying>0` bei `iW==iR` (die Felder bleiben laut Plan im Log) oder als
persistentes `retrying>0 && pending==0 && done==0` ueber N Fenster. In den
Plan als expliziten Arbeitsschritt aufnehmen (tools/serial_monitor.py,
tools/loganalyse.sh), nicht nur als CHANGELOG-Satz.

### F4 — stat_queue_hwm: Belegt-Zaehlung + bestehendes `+1` zaehlt doppelt; Flood-Test und Feld-Semantik brechen

**Datei:** src/txring_functions.cpp:329, 339, 347-348; test/test_txring_flood/test_txring_flood.cpp:265-270
**Severity:** medium

Heute wird `queued` (Distanz) VOR dem iWrite-Fortschritt berechnet — der
gerade geschriebene Eintrag ist nicht enthalten, `stat_queue_hwm =
queued+1` rechnet ihn dazu. Die Belegt-Zaehlung an derselben Stelle
(Zeile 339) faende `ringBuffer[w][0]` aber bereits gesetzt (Zeile 329) —
der neue Eintrag waere schon mitgezaehlt, `+1` zaehlt ihn doppelt: am
vollen Ring ergaebe das hwm == MAX_RING+1. Der Flood-Test pinnt
`stat_queue_hwm == MAX_RING` (Zeile 270) samt dokumentierter Feld-Semantik
"hwm==MAX_RING heisst: Ring voll UND verworfen" (Zeilen 265-269).

**Plan-Korrektur:** Zaehlpunkt definieren: Belegt-Zaehlung unter Ausschluss
von Slot w (dann bleibt `+1` korrekt) ODER inklusive w und `+1` streichen.
Erwartungswerte und Kommentar im Flood-Test anpassen und die
hwm-Feldsemantik im CHANGELOG-Satz mit erwaehnen.

### F5 — Test-Impact-Liste unvollstaendig: test_txring_flood pinnt Tiefe UND BP-Signaturen

**Datei:** test/test_txring_flood/test_txring_flood.cpp:349-361, 389, 398, 412-428
**Severity:** medium

Der Plan nennt als anzupassende Suiten nur test_txring (neu) und
test_backpressure. test_txring_flood (env native_aprs) ruft aber ebenfalls
`bp.onSend(txRingDepth(), ...)` und `bp.poll(txRingDepth())` auf (389, 412,
420, 427-428) — die BP-04-Signaturaenderung bricht diese Suite beim
Kompilieren, sofern `now_ms` keinen Default-Wert bekommt (der Plan sagt
dazu nichts). Zusaetzlich pinnt sie txRingDepth-Werte (349-361, 398, 413,
419, 426) — die bleiben unter Belegt-Zaehlung zufaellig gruen (geprueft:
alle Szenarien ohne Loecher im Fenster), aber F4 (Zeile 270) bricht sicher.

**Plan-Korrektur:** test_txring_flood in die Anpassungsliste aufnehmen;
entscheiden und festschreiben, ob `now_ms` einen Default (`= 0`) bekommt
(dann kompiliert Bestand weiter, aber die Hold-Logik wird bei vergessenem
Durchreichen stumm falsch — besser: kein Default, alle Aufrufer anfassen).

### F6 — BP-03: nichts rueckt iRead vor; Plan-Test 2 erwartet es trotzdem

**Datei:** src/txring_functions.cpp:206-217; src/lora_functions.cpp:1509, 1568; src/esp32/esp32_main.cpp:2459-2470
**Severity:** medium

`advanceIReadPastEmpty()` laeuft nur nach einem erfolgreichen Konsum in
`doTX()` (1568). Gibt BP-03 den Blocker an iRead frei und ist der Rest des
Fensters leer, liefert `getNextTxSlot()` -1 und niemand schiebt iRead nach
— das TX-Gate (`_w != _r`, esp32_main.cpp:2461, nrf52_main.cpp:1397) faehrt
weiter CAD-Zyklen fuer ein leeres Fenster (begrenzt durch den
CAD_FREE_NO_TX-Timeout, aber sinnlos), und Plan-Test 2 ("iRead rueckt vor")
wuerde gegen die geplante Implementierung ROT bleiben, obwohl der Fix
drin ist.

**Plan-Korrektur:** Nach einem Drop am Fenster-Anfang explizit
`advanceIReadPastEmpty()` aufrufen — im per F1 gewaehlten
Main-Loop-Kontext (auf nRF52 unter dem Lock, denn addTxRingEntry schreibt
iRead ebenfalls). Oder die iRead-Erwartung aus Test 2 streichen und das
Vorruecken dem naechsten doTX-Konsum ueberlassen — dann aber das TX-Gate-
Leerlauf-Verhalten bewusst dokumentieren.

### F7 — EXT_PENDING-Slots duerfen nicht altern (Owned-Slot-Invariante)

**Datei:** src/txring_functions.cpp:162-172, 362-368; src/lora_functions.cpp:1808-1813
**Severity:** medium (nur EXTERNAL_RADIO-Builds)

Ein per Bridge in-flight befindlicher Slot (RING_STATUS_EXT_PENDING) ist
per Invariante ueberall vom Freigeben/Altern ausgenommen (drei explizite
Auslassungen im Bestand). Ein eigener HEY kann diesen Status tragen. Der
Plan erwaehnt EXT_PENDING nicht; je nach Einfuegepunkt des Age-Checks im
Scan (vor dem EXT-Skip) wuerde ein >180 s haengender Bridge-TX-Slot
freigegeben und ein spaetes TX_RESULT traefe einen wiederverwendeten Slot.

**Plan-Korrektur:** Explizit festschreiben: Stale-Drop nur fuer Slots mit
Status READY/DONE, nie EXT_PENDING (bei Verlagerung nach
`updateRetransmissionStatus()` per F1 erledigt das der bestehende
`continue` in Zeile 1812 — dann Age-Check dahinter einordnen).

### F8 — BP-03-Altersvergleich ohne uint32-Cast: Rollover- und Native-Falle

**Datei:** Plan Zeile 55; test/support/Arduino.h:56-63
**Severity:** medium

Der Plan schreibt `millis() - ringEnqueueTime[pos] > RING_BG_MAX_AGE_MS`
ohne Cast — fuer BP-04 fordert er die Rollover-Form explizit, fuer BP-03
nicht. Auf nativer Plattform ist `unsigned long` 64 Bit (der Shim liefert
Werte aus einer uint32-Uhr): steht die Uhr kurz vor dem Wrap
(t=0xFFFFFF00) und millis()=5, ergibt die 64-Bit-Differenz eine riesige
Zahl -> Falsch-Drop; auf Hardware dasselbe alle 49,7 Tage. Es gibt sogar
eine eigene Suite test_millis_rollover als Praezedenz.

**Plan-Korrektur:** `(uint32_t)(millis() - ringEnqueueTime[pos]) >
RING_BG_MAX_AGE_MS` festschreiben und einen Rollover-Fall in Plan-Test 3
aufnehmen (mc_test_set_millis nahe UINT32_MAX).

### F9 — "dann continue" ist im heutigen Loop-Koerper eine Endlosschleife

**Datei:** src/txring_functions.cpp:159-191 (pos++ am Schleifenende)
**Severity:** low

Der Scan inkrementiert `pos` am Schleifenende; der EXTERNAL_RADIO-Skip
macht deshalb explizit `pos++; if(pos>=MAX_RING) pos=0; continue;`. Der
Plan sagt fuer den Drop nur "dann continue" — woertlich umgesetzt haengt
der Scan auf dem soeben geleerten Slot fest. (Entfaellt bei Umsetzung von
F1, bleibt als Warnung fuer die Formulierung.)

### F10 — Vereinheitlichungs-Liste uebersieht TX_START qlen

**Datei:** src/esp32/esp32_main.cpp:2559-2562
**Severity:** low

Neben TX_GATE_ENTER (2467) gibt es auf ESP32 auch `TX_START qlen=%d`
(2561), ebenfalls Indexdistanz. Wer qlen vereinheitlicht, muss beide Zeilen
anfassen, sonst zeigen zwei Marker im selben Logfenster zwei Tiefenbegriffe
— genau das, was BP-02 laut Plan beseitigen will.

### F11 — BP-03-Begruendung unpraezise: auch eigene HEY-Beacons altern, und "laengst ersetzt" gilt bei 480 s Trickle nicht

**Datei:** src/loop_functions.cpp:4601-4625 (sendHey -> addTxRingEntry "auto_pos", Status 0xFF); src/configuration_global.h:314
**Severity:** low

Die Klasse Prio 5 umfasst nicht nur HEY-Relays, sondern auch den eigenen
Trickle-HEY (gleicher msg_type 0x40, Status DONE). Bei Trickle-Intervallen
bis 480 s ist ein 3 min alter eigener HEY NICHT "laengst durch neuere
Kopien ersetzt" — die naechste eigene Kopie kann bis zu ~5 min spaeter
kommen; der Knoten bleibt solange unangekuendigt. Der Drop ist trotzdem
vertretbar (ein derart ausgehungerter Ring ist ohnehin der Zustand, in dem
RING_DROP_PRIO BG-Verkehr opfert), aber die Begruendung im Plan und der
"What changes on the air"-Satz muessen den Eigen-Beacon-Fall mit nennen.

### F12 — Staleness-Behauptung von BP-02 zu eng: Relocation kann +/-1 zusaetzlich kosten

**Datei:** src/txring_functions.cpp:403-410
**Severity:** low

"Hoechstens um einen Eintrag stale" unterschlaegt die Eviction-Relocation:
ein Eintrag, der waehrend des lock-freien Zaehl-Scans von r nach worst_slot
umzieht, kann doppelt oder gar nicht gezaehlt werden (Scan-Position
dazwischen). Fehlerband real +/-2. Folgenlos fuer die groben BP-Schwellen,
aber der Code-Kommentar sollte das ehrlich sagen.

### F13 — BP-04: Hold-Zustand in reset()/configure() nicht erwaehnt; Restrisiko QRS/QRV-Zyklen; Hold-Wert knapp

**Datei:** src/backpressure.h:107-117; Plan Zeilen 75-99
**Severity:** low

(a) Die Klasse bekommt neuen Zustand (hold-since, hold-armed);
`reset()`/`configure()` muessen ihn loeschen — der Plan schweigt dazu, und
der bestehende Test "no QRV for an episode that was reset away"
(test_backpressure.cpp:259) haengt genau daran. (b) Bei stetigem Verkehr,
der die Tiefe im Takt >5 s zwischen 1 und 2 pendeln laesst, entsteht ein
QRS/QRV-Zyklus im ~Minutentakt — kein Flap im 08.30-Sinn, aber notierbar.
(c) Zum Wert: 5 s deckt den dokumentierten 400-ms-Flap mit Faktor 12; ein
kompletter TX-Zyklus (CAD-Backoff + Airtime, Bench-Abfluss ~1 Frame/20 s)
kann aber laenger als 5 s dauern — 10 s waeren die robustere Wahl
("laenger als ein TX-Zyklus"), 5 s ist vertretbar, wenn der
Anti-Flap-Regressionstest wie geplant festgenagelt wird. Die 180 s fuer
RING_BG_MAX_AGE_MS sind vertretbar, sollten aber ueber die Drain-Zeit
begruendet werden (Ring-Volldrain ~6 min: 180 s = "haelt eine normale
Burst-Starvation aus, faellt deutlich vor dem 10-min-Blockerfall"), nicht
ueber die faktisch wacklige "laengst ersetzt"-These (F11).

---

## Geprueft und haltbar

- `txRingDepth()` ist exakt die Indexdistanz (txring_functions.cpp:233-238);
  `advanceIReadPastEmpty()` stoppt am ersten belegten Slot (206-217) — ein
  belegter Prio-5-Slot an iRead pinnt den Zeiger, dahinter freigegebene
  Slots zaehlen in die Distanz. Der RCA-Mechanismus (queued=19 bei 3-4
  belegt) ist im Code nachvollziehbar.
- MSG_TYPE_HEY -> MSG_PRIO_BACKGROUND=5 (txring_functions.cpp:90-91,
  configuration_global.h:314); `getNextTxSlot()` waehlt strikt kleinste
  Prio, gleiche Prio FIFO — Prio 5 verliert gegen alles (151-194).
- QRV schliesst heute ausschliesslich bei Tiefe <= CLEAR_DEPTH=0
  (backpressure.h:102, 177, 188); das 08.30-Flapping ist im Header als
  "depth 2 -> 1 -> 2, 400 ms apart" dokumentiert (99-101) — die
  Plan-Darstellung stimmt.
- Zeilenangaben des Plans korrekt: queued-Lokale 339, RING_STATUS
  esp32_main.cpp 2068/2076 und nrf52_main.cpp 1308, TX_GATE_ENTER
  esp32 2467 / nrf52 1414, bpPollDrain/sendMessage loop_functions.cpp
  3478-3510/3808.
- `ringEnqueueTime[worst_slot] = ringEnqueueTime[r]` (407) ist fuer die
  Altersmessung KORREKT: der relozierte Eintrag behaelt seine echte
  Enqueue-Zeit; ein umgezogener stale BG-Eintrag wuerde von BP-03 richtig
  gealtert.
- Retransmit-Pfad (lora_functions.cpp:1886-1891) schreibt eine Kopie in
  einen NEUEN Slot und leert den alten — das Loch in der Mitte existiert
  und stuetzt die BP-02-Motivation. Die Kopie bekommt eine frische
  Enqueue-Zeit, betrifft aber nur MSG_TYPE_TEXT (Nicht-Text wird in
  updateRetransmissionStatus auf DONE gezwungen, 1817-1820) — kein
  BP-03-Konflikt.
- Env-Zuordnung stimmt: test_txring/test_txring_flood in native_aprs
  (platformio.ini:229-241, baut txring_functions.cpp mit
  NATIVE_BUILD-Globals), test_backpressure in env:native (header-only).
  Zeit-Shim `mc_test_set_millis`/`mc_test_advance_millis` existiert
  (test/support/Arduino.h:56-63).
- Zeit-Injektion in BP-04 passt zum Bestand: backpressure.h ist heute
  Arduino-frei, die einzigen produktiven Aufrufer von
  poll()/onSend()/onRefuse() sind loop_functions.cpp:3480/3507/3808; die
  Signaturaenderung trifft in src/ nichts weiteres
  (external_radio_glue's `t.poll(now)` ist eine andere Klasse).
- QUIET_DEPTH=1 kollidiert auch bei MAX_RING=10 (T-Beam,
  configuration_global.h:220) nicht mit refuseThreshold (=8; Klemme
  QUIET_DEPTH+1 in backpressure.h:124-127 greift erst bei absurden
  Ringgroessen).
- Die BP-02-Kernaenderung ist eingegrenzt: txRingDepth speist ausschliesslich
  den BP-Pfad (Log-Zeilen + Statemaschine); Slot-Vergabe/Voll-Erkennung
  bleiben laut Plan indexbasiert — konsistent mit dem Code (353, 428-439).

---

## Verdikt: REWORK

Der Plan trifft die RCA und die Grundrichtung aller drei Fixes, aber die
BP-03-Nebenlaeufigkeitsanalyse ist an zwei Stellen faktisch falsch (F1, F2)
und die Kopplungsliste unvollstaendig (F3-F7). Rework-Liste:

1. (F1) BP-03 aus `getNextTxSlot()` herausnehmen — Aging in den
   2-s-Tick `updateRetransmissionStatus()` verlegen (Main-Loop auf beiden
   Plattformen), Marker nach der kritischen Sektion drucken.
2. (F2) Drop-Bedingung auf nRF52 innerhalb der kritischen Sektion
   re-validieren (len/prio/age) — Check-then-act schliessen; die
   "idempotent, beide schreiben len=0"-Begruendung streichen (Eviction
   relociert).
3. (F3) RING_ZOMBIE-Detektoren in tools/serial_monitor.py und
   tools/loganalyse.sh als expliziten Arbeitsschritt in BP-02 aufnehmen
   (neue Bedingung ueber iW/iR bzw. pending/done); Formulierung "die Summe
   IST die Belegt-Zaehlung" auf Fenster-Belegt-Zaehlung praezisieren.
4. (F4) Zaehlpunkt der queued-Belegt-Zaehlung in addTxRingEntry definieren
   (Slot w aus- oder einschliessen, `+1` entsprechend) und
   test_txring_flood.cpp:270 samt hwm-Feldsemantik in die Anpassungsliste.
5. (F5) test_txring_flood in die Test-Impact-Liste; Entscheidung
   Default-Argument vs. alle Aufrufer anfassen fuer `now_ms` festschreiben.
6. (F6) iRead-Vorschub nach Front-Drop klaeren: advanceIReadPastEmpty im
   sicheren Kontext aufrufen oder die Erwartung aus Plan-Test 2 nehmen.
7. (F7) EXT_PENDING-Ausschluss fuer den Stale-Drop explizit festschreiben.
8. (F8) uint32-Cast im Altersvergleich + Rollover-Testfall ergaenzen.
9. (F10) TX_START qlen (esp32_main.cpp:2561) in die Vereinheitlichung.
10. (F11/F13) Begruendungen nachschaerfen: Eigen-HEY altert mit (On-Air-
    Notiz), 180 s ueber Drain-Zeit statt "laengst ersetzt" begruenden,
    Hold-Zustand in reset()/configure() loeschen; QRV_HOLD_MS 5 s ist
    akzeptabel, 10 s ("> ein TX-Zyklus") waere robuster — Entscheidung
    dokumentieren.

F9 und F12 sind Formulierungs-/Kommentarpunkte, keine eigenen
Rework-Schritte, sofern 1.-2. umgesetzt werden.
