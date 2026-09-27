# Ping faellt jetzt laut aus: Umsetzung und Bench-Nachweis, 2026-09-13

Begleitdokument zu `docs/bug-ping-track-mode-20260913.md` (Fehlerbericht) und zu
BACKLOG 3.8aj. Deutsch, weil der Fehlerbericht es ist.

## Kurzfassung

Der gemeldete Defekt ist behoben und auf drei Boards nachgewiesen -- inklusive des
zweiten, im Bericht nur vermuteten Fehlerwegs (voller/ablehnender Ring). `sendPing()` gibt
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

1. **`[MC-DBG] RADIO_TX len=...` war als Nachweis-Marker nicht tragfaehig** -- inzwischen
   behoben (`47eb2011`). Er stand an **einer** der drei Sendestellen in `doTX()`, und dort
   nur im Nicht-RAK-Zweig: auf dem ESP32 feuerte er fuer normale MeshCom-Frames -- Ping
   inklusive --, nicht fuer TRACK/LoRa-APRS, auf dem RAK nie. Seine Abwesenheit bewies
   also nichts, und genau daran ist der Nachweisteil des Fehlerberichts gescheitert.
   Jetzt an allen sechs Varianten (drei Stellen x RAK/Nicht-RAK) mit einem `kind=`-Feld
   (`track` / `aprs` / `msg`); der Prefix bleibt fuer bestehende Greps unveraendert.
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

### DK5EN-90, RAK4631 -- 4/4

Der Knoten trug zunaechst das Werks-Rufzeichen `XX0XXX-00`. Der Positivpfad wurde nach
Betreiber-Entscheidung mit `--setcall DK5EN-90` nachgeholt (siehe unten).

| Test                          | Ergebnis  | Beleg                                                                                                     |
| ----------------------------- | --------- | --------------------------------------------------------------------------------------------------------- |
| TRACK on (Negativpfad)        | PASS      | Suppressed-Zeile genau 1x, `[PING]...FAILED Ping to DK5EN-93` 3x ueber ~75 s, `grep -c TX_GATE_ENTER` = 0 |
| TRACK off (Positivpfad)       | BLOCKIERT | siehe unten -- kein Defekt des Fixes                                                                      |
| TRACK on erneut (Re-arm)      | PASS      | nach einem vollen TRACK-off-Intervall erneut genau 1x                                                     |
| Info-Gate-Asymmetrie (TEST 4) | PASS      | mit `bDisplayInfo` aus: `send Ping` bleibt weg, `[PING]...FAILED` kommt trotzdem                          |

**Was der zunaechst blockierte TEST 2 nebenbei bewiesen hat.** Mit Werks-Rufzeichen
weist `addTxRingEntry()` jeden Frame unbedingt ab (`isUnconfiguredCall()`,
`src/txring_functions.cpp:449`), noch bevor er den Ring erreicht. `TX_GATE_ENTER` kann auf
diesem Knoten also unabhaengig von TRACK nie feuern. Die beobachtete Kette war:

```
[PING]...send Ping to DK5EN-93
[TX];refuse;unconfigured;ms;...
[PING]...not queued: TX ring refused the frame
[PING]...FAILED Ping to DK5EN-93
```

Das ist der **zweite** neue Rueckgabepfad, `PING_RING_REFUSED` -- auf beiden ESP32-Boards
nie ausgeloest, weil dort der Ring nie abgelehnt hat. Der Fehlerbericht nennt genau diesen
Fall als zweiten Befund ("Bei vollem Ring verschwindet der Ping genauso lautlos"): vorher
haette der Knoten hier `PING nnn / SENT` aufs Display gemalt, `bPingSend` scharf gestellt
und ein Intervall spaeter ein irrefuehrendes `[PONG]...fail` gedruckt. Er sagt jetzt, dass
nichts eingereiht wurde. Der blockierte Test hat damit mehr belegt als der geplante.

**TEST 4 im Detail.** `--debug off` (`command_functions.cpp:2720`) schaltet nur `bDEBUG`
und laesst `bDisplayInfo` unberuehrt -- es ist nicht das Gate, das der Test braucht. Erst
`--loradebug off` setzt `bDisplayInfo` wirklich auf false. Damit blieb die info-gated
`send Ping`-Zeile weg, waehrend `[PING]...FAILED` weiter kam. Genau die Asymmetrie, die
die nRF52-Aenderung garantieren soll.

**Nachgeholt nach Betreiber-Entscheidung.** `--setcall DK5EN-90` gesetzt, mit `ec070235`
neu geflasht, Positivpfad und Ping-Pong-Runde bestaetigt (Zeilen oben). Damit ist auch die
**Marker-Paritaet auf Hardware belegt**: `TX_START qlen=` existiert nur im neuen Stand und
erscheint auf dem Knoten, und die Reihenfolge ist jetzt identisch zum ESP32 --
`TX_GATE_ENTER` -> `CAD_FREE` -> `TX_PREPARE -> TX_ACTIVE` -> `TX_START` -> `TX_DONE`.
Vorher kam `CAD_FREE` erst nach `doTX()` und `TX_START` gar nicht.

Die `build:`-Zeile taugt hier uebrigens nicht als Frischetest: es wurden nur zwei
Uebersetzungseinheiten neu kompiliert, das `__TIME__` der Datei mit der Versionszeile blieb
stehen. Der tragfaehige Nachweis ist das Auftreten von `TX_START` selbst.

Knoten danach aufgeraeumt: `--pingcall NONE`, `--loradebug off`, TRACK off, Rufzeichen
DK5EN-90 bleibt.

## Nachtrag: RADIO_TX repariert und nachgemessen (`47eb2011`)

Alle drei Boards neu geflasht und nachgeprueft:

| Variante                                  | Beleg                                                                                              |
| ----------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `kind=msg`, RAK (`Radio.Send`-Zweig)      | `RADIO_TX len=39 kind=msg` auf DK5EN-90 -- dort hatte es **nie** einen RADIO_TX gegeben            |
| `kind=msg`, ESP32 (`startTransmit`-Zweig) | `RADIO_TX len=39 kind=msg` auf DK5EN-93                                                            |
| `kind=aprs`, ESP32                        | `RADIO_TX len=33 kind=aprs` auf DK5EN-93 -- Pfad ohne vorherigen Marker auf **beiden** Plattformen |
| `kind=track`                              | **nicht beobachtet** -- siehe unten                                                                |

`kind=track` sitzt hinter zwei Bedingungen: ein Positions-Frame im Trackbetrieb **und**
der 5-Minuten-Timer zu MeshCom. Der Knoten steht still, und die Bake laeuft dann mit
`[POSINFO]...STATIONARY --> RATE:1800`, also alle 30 Minuten -- in keinem Testfenster kam
ein Positions-Frame faellig, und `--sendpos` hat keinen erzwungen. Es ist derselbe
Einzeiler in derselben Form wie die vier belegten Varianten; als "beobachtet" auszuweisen
waere zu viel behauptet.

**Nebenbefund, live bestaetigt:** beim Umschalten druckte DK5EN-93
`[PONG]...suppressed: TRACK mode active` -- der `SendPong()`-Marker aus `c570e62e`, den
kein geplanter Test abgedeckt hatte.

**Werkzeug-Lehre (Betreiber-Hinweis):** die Netz-Konsole auf Port 2323
(`tools/hmac_connect.py <ip>`) setzt den Knoten **nicht** zurueck und nimmt Kommandos
entgegen. Damit ist eine Laufzeit > 5 Minuten ueberhaupt erst erreichbar -- ueber die
serielle CP2102-Leitung bootet der Knoten bei jedem Port-Open neu und kommt nie dorthin.
Bei einer zweiten Verbindung blieb `--info` allerdings unbeantwortet, waehrend der
Log-Strom weiterlief; fuer den Abschluss wurde deshalb wieder seriell aufgeraeumt.

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
