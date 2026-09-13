# Ping faellt jetzt laut aus: Umsetzung und Bench-Nachweis, 2026-09-13

Begleitdokument zu `docs/bug-ping-track-mode-20260913.md` (Fehlerbericht) und zu
BACKLOG 3.8aj. Deutsch, weil der Fehlerbericht es ist.

## Kurzfassung

Der gemeldete Defekt ist behoben und auf zwei Boards nachgewiesen. `sendPing()` gibt
jetzt ein Ergebnis zurueck, beide Fehlerwege nennen ihren Grund, und die
TRACK-Unterdrueckung verbraucht kein Ping-Budget mehr. Nebenbei fielen drei Marker
auf, die auf nRF52 und ESP32 dasselbe heissen, aber nicht dasselbe bedeuten; auch das
ist behoben.

Zwei Befunde aus dem Fehlerbericht haben sich beim Nachpruefen **nicht** bestaetigt und
sind unten unter "Korrekturen am Fehlerbericht" festgehalten.

## Was geaendert wurde

| Commit     | Inhalt                                                                               |
| ---------- | ------------------------------------------------------------------------------------ |
| `0d30144b` | Fehlerbericht eingecheckt                                                            |
| `c570e62e` | Ping-Pfad: `PingResult`, Marker, Budget-Regel, Konfigurationswarnungen, `sizeof`-Fix |
| `7de01983` | Bench-Werkzeug: `serial_session.py --boot-marker ready`                              |
| `ec070235` | Marker-Paritaet nRF52 <-> ESP32                                                      |

### Ping-Pfad (`c570e62e`)

`sendPing()` gibt `PingResult` zurueck statt `void`
(`PING_QUEUED` / `PING_SUPPRESSED_TRACK` / `PING_RING_REFUSED`, `src/loop_functions.h:69`).
Ein `bool` reicht nicht: der Aufrufer muss "unterdrueckt" von "Ring hat abgelehnt"
unterscheiden koennen, sonst laesst sich die Budget-Regel nicht formulieren.

- **TRACK-Sperre** druckt `[PING]...suppressed: TRACK mode active (--track off to ping)`
  ueber `printfdeb()`, also unbedingt und unabhaengig von `bLORADEBUG`/`bDisplayInfo`.
  Ratenbegrenzt auf eine Zeile pro Episode, wieder scharf nach TRACK aus/ein.
- **Ring-Ablehnung** druckt `[PING]...not queued: TX ring refused the frame` und kehrt
  **vor** `DisplayPong(SENT)` und **vor** `bPingSend = true` zurueck. Damit kann ein nie
  eingereihter Frame kein `[PONG]...fail` mehr ausloesen.
- **Aufrufer** melden das Ergebnis (`send` / `FAILED`) nach dem Aufruf, statt den Versuch
  vorher anzukuendigen.
- **Budget:** TRACK-Unterdrueckung verbraucht kein `node_pingcount`. TRACK ist ein
  statischer Zustand -- das Budget waere in N x `pingtime` Sekunden leer, ohne dass je
  etwas gesendet wurde, und der Knoten verstummte dauerhaft. Ein abgelehnter Ring ist ein
  echter Versuch und zaehlt mit. (Der Fehlerbericht schlug vor, immer zu dekrementieren;
  bewusst anders entschieden.)
- **Konfigurationszeit:** `--pingcall`, `--ping start` und `--track on` warnen sofort,
  wenn die Kombination nichts senden wird. Das ist der Moment, in dem der Betreiber
  hinschaut; der Intervall-Marker ist nur die Rueckfallebene.
- `SendPong()` trug dieselbe stumme Sperre und hat jetzt denselben Marker (Signatur
  unveraendert).
- `src/command_functions.cpp:3705` schrieb `node_pingcall` mit `sizeof(node_call)`.
  Heute harmlos, beide sind `char[10]`, aber die falsche Schranke.

Die Unterdrueckung selbst bleibt unveraendert und weiterhin konsistent zu `sendMessage()`.

### Marker-Paritaet (`ec070235`)

Beim Schreiben der Bench-Briefings fiel auf, dass die TX-Marker der beiden Plattformen
nicht dasselbe bedeuten:

| Marker     | ESP32                                    | nRF52 vorher                         | jetzt           |
| ---------- | ---------------------------------------- | ------------------------------------ | --------------- |
| `CAD_FREE` | vor `doTX()` -- "Kanal frei"             | im Erfolgszweig -- "Frame ging raus" | vor `doTX()`    |
| `TX_START` | nach erfolgreichem `doTX()`, mit `qlen=` | fehlte                               | ergaenzt        |
| TX-Fehler  | `TX_DONE state=<transmissionState>`      | gar kein MC-SM-Uebergang             | `TX_DONE rc=-1` |

Die `CAD_FREE`-Divergenz ist der eigentliche Defekt: gleicher Name, anderes Ereignis. Auf
dem ESP32 kann `CAD_FREE` von `CAD_FREE_NO_TX` gefolgt werden ("Kanal frei, aber nichts
gesendet"), auf dem nRF52 war diese Kombination unerreichbar -- dort stand
`CAD_FREE_NO_TX` **statt** `CAD_FREE`. Jede flottenweite Auswertung, die `CAD_FREE`
zaehlt, verglich damit zwei verschiedene Groessen.

`rc=0` in `OnTxDone()` ist dagegen **korrekt** und bleibt: der Semtech-Treiber ruft
`OnTxDone()` nur im Erfolgsfall auf und routet Fehler nach `OnTxTimeout()`
(`nrf52_main.cpp:990`). Gefehlt hat, dass der Fehlerpfad gar keinen Zustandsuebergang
druckte und die MC-SM-Spur eines fehlgeschlagenen Sendevorgangs fuer immer bei
`TX_ACTIVE` haengen liess.

Reine Instrumentierung, kein geaendertes Sendeverhalten.

## Korrekturen am Fehlerbericht

Der Fehlerbericht ist in der Diagnose richtig, in zwei Details aber nicht:

1. **`[MC-DBG] RADIO_TX len=...` ist als Nachweis-Marker nicht tragfaehig.** Er existiert
   (`src/lora_functions.cpp:1985`), wird aber nur an **einer** der drei Sendestellen in
   `doTX()` gedruckt, und dort nur im Nicht-RAK-Zweig. Auf dem ESP32 feuert er fuer
   normale MeshCom-Frames -- Ping inklusive, im Bench auch gesehen --, nicht fuer
   TRACK/LoRa-APRS; auf dem RAK nie. Seine Abwesenheit beweist also nichts.
2. **Die nRF52-Kette ist kuerzer.** Kein `TX_START`, kein `TX_DONE state=` (vor
   `ec070235`). Der Positivnachweis lautet dort `TX_GATE_ENTER` + `CAD_FREE` +
   `CHANNEL_UTIL tx!=0`.

Beides ist kein Fehler der Analyse -- die Beweiskette des Berichts haengt an der
**Abwesenheit** von `TX_GATE_ENTER`, und die traegt auf beiden Plattformen.

## Bench-Nachweis

Drei Knoten, je ein eigener serieller Port, parallel getestet. Firmware aus `c570e62e`,
vor dem Test frisch geflasht (Build-Zeile in `--info` geprueft).

Voraussetzungen je Knoten: `--loradebug on` (die `[MC-DBG]`-Marker haengen an
`bLORADEBUG`; setzt nebenbei `bDisplayInfo=true`, `command_functions.cpp:2800`),
`--pingtime 30` (die ESP32-Schleife laeuft erst ab `> 29`), `--pingcall` auf einen
Nachbarknoten -- nie ein Broadcast-Ziel.

### DK5EN-14, T-Deck Plus -- 3/3

| Test                     | Ergebnis | Beleg                                                                                                                                           |
| ------------------------ | -------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| TRACK on (Negativpfad)   | PASS     | Suppressed-Zeile genau 1x, `[PING]...FAILED Ping to DK5EN-93 <5>` 3x, `grep -c TX_GATE_ENTER` = 0, `Count` 5->5                                 |
| TRACK off (Positivpfad)  | PASS     | `send Ping`, `TX_GATE_ENTER qlen=1`, `CAD_FREE`, `RADIO_TX len=39`, `TX_START qlen=0`, `TX_DONE state=0`, `CHANNEL_UTIL tx=561ms`, `Count` 5->3 |
| TRACK on erneut (Re-arm) | PASS     | Suppressed-Zeile erneut genau 1x                                                                                                                |

### DK5EN-93, Heltec V3 -- 3/3

| Test                     | Ergebnis | Beleg                                                                                                                                     |
| ------------------------ | -------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| TRACK on (Negativpfad)   | PASS     | Suppressed-Zeile 1x, `FAILED ... <5>` 2x (Zaehler unveraendert), `TX_GATE_ENTER`/`TX_START`/`RADIO_TX` je 0                               |
| TRACK off (Positivpfad)  | PASS     | `send Ping <5>` dann `<4>`, volle TX-Kette, `CHANNEL_UTIL tx=561ms`, und ein echter Rueckweg: `[PONG] from:DK5EN-14 <271> rssi:-26 snr:7` |
| TRACK on erneut (Re-arm) | PASS     | Suppressed-Zeile erneut 1x                                                                                                                |

**Ping-Pong ist damit als Runde belegt**, nicht nur als Sendevorgang.

### DK5EN-90, RAK4631 -- laeuft noch

Nachtrag folgt. Getestet wird dieselbe Matrix plus TEST 4: die neue
`[PING]...FAILED`-Zeile steht auf dem nRF52 bewusst **ausserhalb** von `bDisplayInfo`,
waehrend die bestehende `send Ping`-Zeile dahinter bleibt -- mit Info aus muss die
Fehlermeldung trotzdem kommen. Die Marker-Paritaet aus `ec070235` ist auf Hardware
ebenfalls noch nicht bestaetigt (Image gebaut, Marker per String-Scan im Image
nachgewiesen, aber nicht auf dem Knoten gesehen).

### Ein Nachweis, der nur auf nativem USB gelingt

Der Reset des Ratenbegrenzer-Flags (`loop_functions.cpp:3330`) ist auf einem CP2102-Board
**nicht** beweisbar: dort bootet der Knoten bei jedem Port-Open neu und setzt den `static`
ohnehin zurueck. Auf dem T-Deck (natives USB, kein Reset beim Open -- belegt dadurch, dass
`Count` 5->3 ueber Sitzungsgrenzen hinweg erhalten blieb) kam die Suppressed-Zeile nach
einem erfolgreichen Senden erneut. Das geht nur, wenn die Reset-Zeile wirklich laeuft.

## Werkzeug-Fund

`serial_session.py --wait-boot` akzeptierte `CLIENT STARTED` als Boot-Marker. Diese Zeile
erscheint auf einem ESP32, der noch WiFi hochfaehrt, rund acht Sekunden vor
`[BOOT];ready` -- das erste Kommando ging also in einen bootenden Knoten und war weg. Der
Lauf sah dabei gesund aus: der Knoten loggte weiter, der Byte-Zaehler stimmte, nur die
Antwort fehlte. `--boot-marker ready` schraenkt die Wartung auf `[BOOT];ready` ein
(`7de01983`); Vorgabe bleibt `any`, bestehende Aufrufer aendern sich nicht.

Ohne diesen Fund waere ein verschlucktes `--info` als "der Knoten hat den Flash nicht
genommen" gelesen worden.

## Was offen bleibt

Siehe BACKLOG 3.8aj fuer die vollstaendige Tabelle. Die Punkte mit Substanz:

- **PING-01** -- die nRF52-Ping-Schleife ist von der ESP32-Variante abgedriftet (kein
  Budget, kein `PongFail()`). Zusammenfuehren ist eine eigene Aenderung.
- **PING-02** -- `node_pingcount` wird nicht persistiert, `--ping start` ruft aber
  `save_settings()`. Nach einem Neustart ist die Schleife still. Fuer ein
  Diagnosewerkzeug vertretbar, aber so nicht erkennbar.
- **TXM-01** -- `doTX()` traegt drei nahezu identische Sendestellen, jede nochmals nach
  `BOARD_RAK4630` gegabelt. Genau daher die Marker-Divergenz: es gibt keinen **einen** Ort
  zum Instrumentieren. Gehoert zu 3.8af (DRY), nicht als Einzelpatch.
- **TXM-03** -- der ESP32 pollt vor dem CAD-Scan das SX1262-IRQ-Register und bricht den TX
  ab, wenn ein Paket im Anflug ist; der nRF52 hat kein Gegenstueck. **Hypothese aus dem
  Code-Lesen, nicht verifiziert** -- ob `Radio.StartCad()` den Fall ohnehin faengt, braucht
  einen Bench-Test, keine weitere Codelektuere.

Der Ping-Fix ist ein Upstream-Defekt, kein Fork-Spezifikum, und damit PR-wuerdig gegen
icssw-org DEV.
