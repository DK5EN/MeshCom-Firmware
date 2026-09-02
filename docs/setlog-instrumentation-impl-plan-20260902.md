# Implementierungsplan SL-01..SL-07 — Messpunkte unter `--setlog on`

**Status: ENTWURF 2026-09-02, noch nicht begonnen.** Umsetzung mit
`/orchestrate-waves`; der Plan ist so geschnitten, dass jede Welle disjunkte
Dateien hat und der Orchestrator die Hotspots selbst anfasst.

Herkunft: Nachtmessung der OE3-Bergknoten 1./2.09.2026
(`docs/report-2026-09-02-oe3-bergknoten.htm`, Abschnitt "Debug-Flags") und
Konzeptbericht `docs/report-2026-09-02-adaptiver-relay-slot.htm` (PR-A).
Code-Stellen geprüft gegen `v4.35p_prio` @ `3b34dfc1`.

## Ziel

Die drei Berg-Gateways (OE3XIR-12, OE3XOC-12, OE3XWJ-12) und OE3MAG-12 laufen auf
Upstream-DEV und werden von einem Raspberry Pi über USB-Seriell mitgeschnitten.
Dort ist dauerhaft nur `--setlog on` vertretbar; `--loradebug on` erzeugt
10-Sekunden-Zeilen, `[MC-SM]`-Übergänge und Heap-Churn. Heute liefert `--setlog`
genau eine Zeile je Empfang, ohne Pegel, ohne Dedup-Verdikt, ohne
Relay-Entscheidung, ohne eigene Sendungen. Der Plan erweitert `--setlog` so, dass
`tools/berglog.py` aus einem Mitschnitt folgende Fragen beantworten kann, die
im Nachtbericht offen bleiben mussten:

| Frage                                       | heute                    | nach SL-01..SL-07                                             |
| ------------------------------------------- | ------------------------ | ------------------------------------------------------------- |
| Pegel je Paket, Rauschboden, Bimodalität    | nicht messbar (RAK)      | `RSSI:`/`SNR:` in der RX-Zeile                                |
| Dedup-Fenster, Zombie-Relays                | Schätzung über Kopien    | `DUP:` in der RX-Zeile, `newid=`/`dup=` im Statusblock        |
| Warum wurde nicht relayt                    | unbekannt                | `RLY`-Zeile mit `reason=`                                     |
| Warteschlangenlatenz, Starvation, CAD       | nur über Nachzügler      | `TX`-Zeile mit `wait=`, `cad=`, `q=`                          |
| Kollisionsrate                              | nur ESP32, nur loradebug | `ERR`-Zeile plattformunabhängig                               |
| Kanalauslastung, Ringfüllstand, Drops, Heap | nur loradebug, 10 s      | `STAT`-Zeile alle 5 min                                       |
| Gateway-Vervielfachung                      | nur `[GATE]` ohne ID     | `GWI`/`GWU`-Zeilen mit msg_id                                 |
| Untervariante der Firmware                  | nicht unterscheidbar     | `fw=`/`flash=` im Statusblock (nur lokal; Luft: SL-08, offen) |

## Rahmenbedingungen (bindend für jeden Agenten)

1. **Alle neuen Zeilen hängen an `bDisplayLog`** (`--setlog on`, persistiert in
   `node_sset4 & 0x0004`). Nichts davon unter `bLORADEBUG`, nichts unbedingt.
2. **Upstream-fähig.** Die Gateways laufen auf Upstream-DEV; der Diff geht als
   PR dorthin (deutsche Beschreibung, `/submit-pr`). Deshalb: keine Abhängigkeit
   von Fork-Sonderpfaden. Vor Welle 1 prüft der Orchestrator, dass
   `ringEnqueueTime[]`, `stat_drop_count[]`, `ch_util_*_accum` und
   `PRIO_STAT_INTERVAL_S` in `upstream/dev` existieren
   (`git diff upstream/dev -- src/txring_functions.cpp src/loop_functions.cpp`).
   Fehlt etwas, wird es im PR mitgeliefert, nicht vorausgesetzt.
3. **Keine Semikolons als Trenner.** `printfdeb()` schreibt `;` außerhalb des
   CSV-Modus um (`src/printfdeb_format.h:25-82`). Die `[ETH]`/`[GW]`-Zeilen
   überleben nur, weil sie `Serial.printf` direkt rufen, und das erreicht auf
   ESP32 die 2323-Konsole nicht. Alle neuen Zeilen: `printfdeb`, Felder als
   `key=value`, Leerzeichen als Trenner, wie die bestehende `[LOG]`-Zeile.
4. **Zeilen kurz halten.** `loc_buf` in `printfdeb_functions.cpp:86` hat 600
   Byte; darüber wird `malloc` gerufen. Ziel: RX-Zeile bleibt unter 300 Byte
   auch mit langem Pfad, alle anderen unter 160 Byte.
5. **Ausführungskontext beachten.** `OnRxDone()` läuft auf nRF52 im
   FreeRTOS-Timer-Task (Prio 2, 1 KB Stack) oder im LORA-Task
   (`docs/architecture/09-concurrency-map.md:72-106`); die bestehende
   `[LOG]`-Zeile wird dort schon gedruckt. Neue Zeilen in diesem Kontext
   (RX, RLY, ERR) dürfen keine zusätzlichen Puffer auf dem Stack anlegen: die
   Formatierer aus SL-00 schreiben in einen übergebenen Puffer, der Aufrufer
   nutzt den bestehenden. `TX` läuft in `doTX()` (LORA-Task / loop), `STAT`
   und `GW*` in `loop()`.
6. **Zähler sind `std::atomic<uint32_t>`** wie `ch_util_rx_accum`
   (`src/loop_functions.cpp:431-434`), weil Timer-Task und loop sie
   gleichzeitig anfassen.
7. **Bestehende Felder der `[LOG]`-Zeile bleiben byteidentisch** in Reihenfolge
   und Format. Neue Felder werden angehängt. `tools/berglog.py` und die
   Fixtures müssen beides lesen (alt ohne, neu mit Anhang).
8. **Keine Verhaltensänderung.** Dieser Plan ist reine Instrumentierung. Die
   Fixes aus den Berichten (HEY-Hop-Budget, Dedup-Zeitalterung, bare-R-Parser,
   RX-01 auf nRF52) sind eigene PRs und werden hier nicht mitgenommen.

## Die Zeilen im Einzelnen

Alle Zeilen beginnen wie heute mit `HH:MM:SS [LOG] `. Danach ein Kennwort,
das die Zeilenart benennt. Die bestehende Empfangszeile bekommt kein Kennwort,
damit alte Parser sie weiter erkennen (`x<id> H<hop>` an fester Position).

### SL-01 — RX-Zeile erweitern

Stelle: `src/lora_functions.cpp:573-582` (`OnRxDone`, Aufruf
`printBuffer_aprs("[LOG]", aprsmsg)`), Formatierer `printBuffer_aprs()` in
`src/loop_functions.cpp:3144-3148`. `rssi` und `snr` sind an der Stelle im
Scope (Parameter von `OnRxDone`). Das Dedup-Verdikt fällt heute erst bei
`is_new_packet(RcvBuffer+1)` an `src/lora_functions.cpp:797`; `is_new_packet()`
ist eine reine Suche ohne Seiteneffekt (`src/dedup_functions.cpp`), darf also
vor dem Druck einmal zusätzlich aufgerufen werden. Der Eintrag ins Ring
(`addLoraRxBuffer`) bleibt, wo er ist.

Angehängte Felder, in dieser Reihenfolge:

```
 RSSI:<int> SNR:<int> DUP:<n|d> OWN:<-|e> t=<millis>
```

- `DUP:d` = msg_id war schon im Dedup-Ring (Kopie), `n` = neu.
- `OWN:e` = eigenes Rufzeichen steht im Pfad (Echo eines eigenen Relays), sonst `-`.
  Die Suche existiert schon in `src/lora_functions.cpp:1284-1292`
  (`searchPath.indexOf(searchCall)`); der Formatierer bekommt das Ergebnis als
  bool, nicht den Pfad.
- `t=` = `millis()`, damit Kopienabstände auch ohne Host-Zeitstempel messbar sind
  (die Knotenuhr hat 1 s Auflösung).

Neue Signatur, alte bleibt für `MH-LoRa`/`RX-UDP` unverändert:

```c
void printBuffer_aprs_rx(const char *msgSource, struct aprsMessage &aprsmsg,
                         int16_t rssi, int8_t snr, bool dup, bool own_echo);
```

Die ACK-Variante `printBuffer_ack()` (`src/loop_functions.cpp:3151`) bekommt
dieselben vier Anhänge (`DUP:` aus `is_new_packet(print_buff+1)`, das an
`src/lora_functions.cpp:310` ohnehin gerufen wird — Reihenfolge so ändern,
dass das Verdikt vor dem Druck vorliegt).

### SL-02 — RLY-Zeile: die Relay-Entscheidung mit Grund

Stelle: Relay-Block `src/lora_functions.cpp:1200-1356`. Eine Zeile je
**neuem** Frame (nicht für Duplikate — die enden an `:797` und erzeugen keine
Entscheidung). Format:

```
[LOG] RLY x<id> <typ> H<hop> q=<code> prio=<n> slot=<n>
```

`q=tx` wenn das Relay in den Ring geschrieben wurde (dann `prio=` und `slot=`
aus `addTxRingEntry`), sonst der Grund, genau einer, in der Reihenfolge der
Prüfung im Code:

| code       | Bedingung                                               | Stelle                                 |
| ---------- | ------------------------------------------------------- | -------------------------------------- |
| `unconf`   | Ursprung mit Werksrufzeichen                            | `src/lora_functions.cpp:589-595`       |
| `self`     | Ziel ist das eigene Rufzeichen                          | `:1269`                                |
| `aprs`     | `bSetLoRaAPRS`                                          | `:1269`                                |
| `nomesh`   | `checkMesh()` false (Typ nicht meshfähig oder MESH aus) | `:1269`, `src/via_functions.cpp:82-87` |
| `gwfilter` | WLNK-1 / APRS2SOTA / 100001-Sonderziele                 | `:1222-1228`                           |
| `gwcap`    | Pfadlänge über `max_hop_text+1` bzw. `max_hop_pos+1`    | `:1231-1234`                           |
| `ping`     | `{ping}` an 100001                                      | `:1241-1242`                           |
| `hop0`     | `max_hop == 0`                                          | `:1272`                                |
| `loop`     | eigenes Rufzeichen schon im Pfad                        | `:1284-1292`                           |
| `full`     | `addTxRingEntry` konnte nicht einreihen                 | Rückgabewert prüfen                    |

Umsetzung: eine lokale `const char *rly_reason = NULL;` am Blockanfang, an
jeder Ausstiegsstelle gesetzt, ein einziger `printfdeb` am Blockende
(`skip_relay:`-Label existiert bereits an `:1291`). Damit bleibt der Block
frei von verstreuten Druckaufrufen.

### SL-03 — TX-Zeile: jede eigene Sendung

Stelle: `doTX()` `src/lora_functions.cpp:1548-1558`, direkt nach dem Slot-Lesen;
dort wird an `:1514` schon `latency = millis() - ringEnqueueTime[txSlot]`
gerechnet. Format:

```
[LOG] TX x<id> <typ> H<hop> prio=<n> src=<o|r|g> wait=<ms> q=<depth> cad=<n> len=<bytes> t=<millis>
```

- `src=o` eigene Nachricht, `r` Relay (`ringBuffer[slot][1] == RING_STATUS_DONE`),
  `g` vom Server eingespeist (Kennzeichnung kommt aus SL-06: ein Byte
  `ringSource[MAX_RING]`, gesetzt in `addTxRingEntry` über den bestehenden
  `source`-String, der heute nur in `RING_WRITE … src=` gedruckt wird).
- `wait=` Einreihung bis Luft in ms, `q=` `txRingDepth()` nach dem Entnehmen.
- `cad=` Zahl der CAD-Versuche bis zu dieser Sendung. nRF52: `cad_attempt`
  (`src/nrf52/nrf52_main.cpp:426`, static; Getter `getCadAttempt()` in
  `nrf52_main.cpp` ergänzen). ESP32: die Variable des TX-Gates in
  `src/esp32/esp32_main.cpp:2511` (`TX_GATE_ENTER … cad_attempt=`), gleicher
  Getter-Name. Wo kein Wert vorliegt, `cad=0`.
- Die Zeile wird beim erfolgreichen Start der Sendung gedruckt, nicht bei
  `TX_DONE`; ein Abbruch durch Watchdog erscheint als fehlende Folgezeile und
  im `STAT`-Block als `txfail=`.

### SL-04 — ERR-Zeile: verlorene Frames, plattformunabhängig

Stellen: `OnRxError()` `src/lora_functions.cpp:1452-1490` (nRF52: RSSI/SNR aus
`RadioPktStatus`, Länge nicht verfügbar) und die ESP32-CRC-Stelle
`src/esp32/esp32_main.cpp:4048-4164` (RSSI, SNR, Frequenzfehler, Länge). Format:

```
[LOG] ERR rssi=<int> snr=<int> len=<n|0> ferr=<hz|0> t=<millis>
```

Beide Stellen rufen denselben Formatierer aus SL-00. Zusätzlich zählt
`stat_rx_err` (atomic) für den Statusblock. Die bestehende
`[MC-DBG] CRC_ERROR`/`RX_ERROR`-Ausgabe unter `bLORADEBUG` bleibt unverändert.

### SL-05 — STAT-Zeile alle fünf Minuten

Stelle: der bestehende 5-Minuten-Tick `PRIO_STAT_INTERVAL_S`
(`src/configuration_global.h:345`), ESP32 `src/esp32/esp32_main.cpp:2149-2166`,
nRF52 `src/nrf52/nrf52_main.cpp:~1352`. Der Block druckt heute unter
`bLORADEBUG`; die neue Zeile druckt unter `bDisplayLog` daneben. Format, eine
Zeile:

```
[LOG] STAT util=<pct> rx=<ms> tx=<ms> newid=<n> dup=<n> err=<n> txn=<n> txfail=<n>
      ringmax=<n>/<MAX_RING> drop=<p1>/<p2>/<p3>/<p4>/<p5> mh=<n> heap=<bytes>
      trk=<interval_s>/<consistent> fw=<major><sub>/<flash> up=<s> t=<millis>
```

- `util=` wie `CHANNEL_UTIL`, aber über das 5-min-Fenster (eigene Akkumulation
  aus `ch_util_rx_accum`/`tx_accum`, die 10-s-Logik nicht anfassen).
- `newid=`/`dup=` aus zwei neuen Zählern, inkrementiert an der Stelle des
  Dedup-Verdikts (SL-01). Daraus folgt das Ringfenster direkt:
  `MAX_DEDUP_RING / (newid / 300 s)`.
- `err=` aus SL-04, `txn=` aus SL-03, `txfail=` aus dem TX-Watchdog
  (`TX_WATCHDOG_MS`-Pfad).
- `ringmax=` Hochwasser von `txRingDepth()` im Intervall (in `addTxRingEntry`
  mitführen), `drop=` aus `stat_drop_count[]` (`src/loop_functions.cpp:476`,
  Reset nach dem Druck).
- `mh=` `getMheardCount()`, `heap=` ESP32 `ESP.getFreeHeap()`, nRF52
  `dbgHeapFree()` bzw. die Funktion hinter der `[HEAP]`-Zeile
  (`src/web_functions/web_functions.cpp:552` ist ESP-only — für nRF52 die
  `mallinfo()`-Variante aus `dbgHeapTotal` nutzen, falls vorhanden, sonst 0).
- `trk=` Trickle-Zustand: nRF52-Variablen sind static in `nrf52_main.cpp:1927-1936`;
  Getter `getTrickleState(uint32_t*, int*)` ergänzen, ESP32 analog
  (`esp32_main.cpp:~3340`).
- `fw=` `shortVERSION()`, `shortSUBVERSION()` (`src/aprs_functions.cpp:12-19`)
  und `FLASH_VERSION` (`src/configuration_global.h:81`). Damit ist die
  Untervariante im Mitschnitt sichtbar, ohne die Luft zu ändern.

Alle Intervallzähler werden nach dem Druck auf null gesetzt (`exchange(0)`).

### SL-06 — GW-Zeilen: Einspeisung und Upload

Stellen: Server-zu-LoRa `src/udp_functions.cpp:361-479` (ESP32/WiFi) und
`src/nrf52/nrf_eth.cpp:434-620` (RAK Ethernet), Upload `addNodeData()`-Aufruf
`src/lora_functions.cpp:1263`. Formate:

```
[LOG] GWI x<id> <typ> H<hop> from=<call> t=<millis>
[LOG] GWU x<id> <typ> H<hop> t=<millis>
```

`GWI` beim Einreihen des Server-Frames in den TX-Ring (dort auch
`ringSource[w] = 'g'` für SL-03), `GWU` unmittelbar vor `addNodeData`. Damit ist
die Vervielfachung durch mehrere Gateways und die Reihenfolge Upload-vor-
Dekrement (`:1263` vor `:1278`) messbar.

### SL-07 — Werkzeugseite

- `tools/berglog.py`: Parser für die angehängten RX-Felder (optional, abwärts
  kompatibel), neue Zeilenarten `RLY`, `TX`, `ERR`, `STAT`, `GWI`, `GWU`;
  neue Abschnitte: Dedup-Fenster direkt (`newid`), Relay-Gründe, Wartezeit je
  Prio und Typ, Kollisionsrate, Kanalauslastung je 5 min, Gateway-Multiplikator,
  RSSI/SNR-Verteilung und Rauschboden (`RSSI - SNR` über Fernempfänge). Die
  Fixtures `tools/testdata/berglog_sample_*.txt` bekommen je Zeilenart
  Beispiele; `tools/mock/test_berglog.py` prüft alt und neu.
- `.claude/skills/logauswertung/SKILL.md`: Zeilenreferenz und die Regel
  "RX-Zeile ohne `DUP:` heißt alte Firmware, dann Kopienzählung als Ersatz".
- `docs/BACKLOG.md`: Einträge SL-01..SL-07, `docs/CHANGELOG-stability.md`.

### SL-08 — Build-Kennung auf der Luft (offen, nicht Teil dieses Plans)

`FW:35:p` ist für Upstream-März-Build und Fork identisch. Ein Anhang in der
HEY-Nutzlast (nachlaufende Gruppe `B<flash>;`, alte Parser ignorieren sie) oder
ein Feld im Positionskommentar (`src/loop_functions.cpp:4280-4322`) braucht
Kurts Zustimmung und einen mcmap-Parser. Erst nach SL-01..07 ansprechen; die
lokale `fw=`-Angabe in SL-05 deckt den Bedarf der Mitschnitte.

## Neue Zustände und Kosten

| Name                                     | Typ                     | Datei                               | RAM (RAK4631) |
| ---------------------------------------- | ----------------------- | ----------------------------------- | ------------- |
| `ringSource[MAX_RING]`                   | `uint8_t`               | `src/txring_functions.cpp` + extern | 20 B          |
| `stat_newid`, `stat_dup`                 | `std::atomic<uint32_t>` | `src/loop_functions.cpp`            | 8 B           |
| `stat_rx_err`, `stat_txn`, `stat_txfail` | `std::atomic<uint32_t>` | dito                                | 12 B          |
| `stat_ring_max`                          | `std::atomic<uint8_t>`  | dito                                | 1 B           |
| `stat_util_rx/tx_5m`                     | `std::atomic<uint32_t>` | dito                                | 8 B           |
| Formatierer                              | Funktionen              | `src/setlog_lines.cpp/.h`           | Flash ~1,5 kB |

Gesamt unter 64 Byte RAM. Kein neuer Puffer; alle Formatierer schreiben in den
vom Aufrufer gestellten `char buf[]` (bei RX/RLY/ERR der Puffer, der heute
schon für `printfdeb` benutzt wird).

## Wellenplan für `/orchestrate-waves`

### Welle 0 — Fundament (ein Agent, Opus/hoch, oder Orchestrator selbst)

Exklusiv: `src/setlog_lines.h`, `src/setlog_lines.cpp`, `src/loop_functions.cpp`
(nur Zählerdefinitionen und `printBuffer_aprs_rx`), `src/loop_functions_extern.h`
(externs), `src/txring_functions.cpp/.h` (nur `ringSource[]` setzen in
`addTxRingEntry`, Hochwasser mitführen), `test/test_setlog_lines/`.

Inhalt: alle Formatierer als reine Funktionen `int setlogFormatXxx(char *buf,
size_t n, ...)`, die Zähler, die Getter-Deklarationen
(`getCadAttempt`, `getTrickleState`, `getHeapFree`) als schwache Defaults, die
native Testsuite für jeden Formatierer (Länge unter Limit, Feldreihenfolge,
Grenzwerte, alte RX-Zeile byteidentisch als Präfix). Gate: `pio test -e native`
(alle native-Umgebungen, die die Testsuite listet), Build `wiscore_rak4631` und
`heltec_wifi_lora_32_V3`. Commit.

### Welle 1 — Aufrufstellen (vier Agenten parallel, Sonnet/hoch, disjunkt)

| Agent | Exklusive Dateien                                | Aufgaben                                                                         |
| ----- | ------------------------------------------------ | -------------------------------------------------------------------------------- |
| A     | `src/lora_functions.cpp`                         | SL-01 RX, SL-02 RLY, SL-03 TX, SL-04 nRF52-Seite, SL-06 GWU                      |
| B     | `src/esp32/esp32_main.cpp`                       | SL-04 ESP32-CRC-Seite, SL-05 STAT-Tick, `getCadAttempt`, `getTrickleState` ESP32 |
| C     | `src/nrf52/nrf52_main.cpp`                       | SL-05 STAT-Tick, `getCadAttempt`, `getTrickleState`, `getHeapFree` nRF52         |
| D     | `src/udp_functions.cpp`, `src/nrf52/nrf_eth.cpp` | SL-06 GWI beide Plattformen, `ringSource='g'`                                    |

Hotspot-Carve-out: `src/configuration_global.h` und `src/loop_functions_extern.h`
fasst in Welle 1 kein Agent an; braucht ein Agent eine Konstante oder ein
extern, meldet er es, der Orchestrator trägt es am Gate nach. Jeder Agent
verifiziert nur seine Datei (`pio run -e <env>` seiner Plattform, `grep -c` der
neuen Kennwörter). Gate nach der Welle durch den Orchestrator: alle sechs
Board-Envs (`wiscore_rak4631`, `heltec_wifi_lora_32_V3`, `ttgo_tbeam`,
`t_deck_plus`, `E22-DevKitC`, plus eine ESP32-classic-Variante), alle native
Tests, `tools/ram_snapshot.py` gegen `tools/resource_baseline.json` (kein
Board über Budget). Commit.

### Welle 2 — Werkzeug und Doku (zwei Agenten parallel)

| Agent | Exklusive Dateien                                                                         | Aufgaben                           |
| ----- | ----------------------------------------------------------------------------------------- | ---------------------------------- |
| E     | `tools/berglog.py`, `tools/mock/test_berglog.py`, `tools/testdata/berglog_sample_*.txt`   | SL-07 Parser, Abschnitte, Fixtures |
| F     | `.claude/skills/logauswertung/SKILL.md`, `docs/BACKLOG.md`, `docs/CHANGELOG-stability.md` | Zeilenreferenz, Einträge           |

Gate: `uvx mypy`, `uvx ruff check --select E9,F,W`, `python3 -m unittest
discover -s tools/mock`, `npx --yes prettier@3 --check` auf den Markdown-Dateien.
Commit.

### Welle 3 — Benchnachweis (Orchestrator)

1. RAK4631 DK5EN-90 flashen (`/flash-rak`), `--setlog on`, 30 Minuten
   `tools/serial_monitor.py` mitschneiden, parallel Heltec-93 mit demselben Stand.
2. Prüfen, dass jede Zeilenart vorkommt (`grep -c '\[LOG\] RLY'` usw.), dass
   die RX-Zeile unter 300 Byte bleibt (`awk 'length>300'` leer), und dass
   `tools/berglog.py` den Mitschnitt ohne `undecodable` verarbeitet.
3. Gegenprobe: dieselbe halbe Stunde mit `--loradebug on` auf dem Heltec;
   `newid`/`dup` aus `STAT` müssen zu `RX_DEDUP_ADD`/`RX_DEDUP_DUP` passen,
   `wait=` zu `RING_TX_READ … latency`.
4. Heap: `heap=` über 30 Minuten flach; NimBLE-Verbindung am Heltec bleibt
   (vgl. Memory "printf malloc starves NimBLE").

### PR-Verpackung

Ein PR gegen `icssw-org/MeshCom-Firmware` DEV mit deutscher Beschreibung
(Was: die sieben Zeilen mit Formatreferenz; Warum: Nachtbericht OE3, die drei
offenen Fragen Pegel, Dedup, Warteschlange; Kosten: RAM-Tabelle, kein
Verhaltenswechsel, alles hinter `--setlog`). Vorher `/rebase-upstream` und
die Prüfung aus Rahmenbedingung 2. Nach dem Merge: OE1KBC/OE3MZC bitten, die
vier Berg-Gateways zu aktualisieren und `--setlog on` zu setzen; der Pi
schneidet weiter mit.

## Tests

- `test/test_setlog_lines/`: je Formatierer ein Fall mit typischen Werten, ein
  Fall mit Maximalwerten (Pfad mit acht Rufzeichen, negative Pegel), Prüfung
  der Länge und dass die alte RX-Zeile als Präfix byteidentisch bleibt
  (Vergleich gegen `test/support/traces/` Beispielzeile aus dem OE3-Mitschnitt).
- `test/test_txring/`: `ringSource[]` wird bei `addTxRingEntry` gesetzt und bei
  Verdrängung mitkopiert (`src/txring_functions.cpp:542` kopiert
  `ringEnqueueTime` — dort auch `ringSource`).
- `tools/mock/test_berglog.py`: Fixture mit alten und neuen Zeilen, beide
  Varianten liefern dieselben Empfangszahlen; neue Abschnitte gegen
  handgerechnete Werte.
- Fails-before: ein Test, der `DUP:` in der RX-Zeile erwartet, schlägt auf dem
  aktuellen Stand fehl.

## Reihenfolge und Aufwand

| Schritt | LOC (Schätzung)               | Abhängig von |
| ------- | ----------------------------- | ------------ |
| Welle 0 | ~220 + Tests                  | —            |
| Welle 1 | ~180 (A 90, B 35, C 35, D 20) | Welle 0      |
| Welle 2 | ~300 Python + Doku            | Welle 1      |
| Welle 3 | Bench                         | Welle 2      |

## Risiken

- **Heap-Churn auf ESP32.** Jede Zeile über 600 Byte ruft `malloc` in
  `printfdeb`. Die RX-Zeile mit Pfad aus acht Rufzeichen und 100 Byte
  Nutzlast liegt heute bei ~260 Byte; der Anhang addiert unter 45 Byte. Der
  Längentest in Welle 0 ist die Sicherung.
- **Timer-Task-Stack auf nRF52 (1 KB).** Kein neuer Puffer im Kontext von
  `OnRxDone`; die RLY-Zeile nutzt denselben Puffer nach dem RX-Druck. Bench in
  Welle 3 beobachtet `[INSTR-LOOP]` und Resets.
- **Seriell-Durchsatz am Pi.** 800 Empfänge je Stunde mal 2,3 Zeilen mal 200
  Byte sind rund 100 Byte je Sekunde; unkritisch bei 115200 Baud.
- **Doppelter Dedup-Lookup** (SL-01) kostet eine Ringsuche über 100 Einträge
  je Empfang; bei 0,2 Empfängen je Sekunde vernachlässigbar. Alternative, falls
  der Reviewer es nicht will: Verdikt an `:797` in eine Variable schreiben und
  den RX-Druck hinter die Entscheidung verschieben — dann fehlen aber die
  Zeilen für Frames, die vorher verworfen werden (`unconf`).
- **Upstream-Abweichung.** Fehlt `ringEnqueueTime` in DEV, ist SL-03 ohne
  `wait=` sinnlos; der Vorabcheck in Rahmenbedingung 2 entscheidet, ob der PR
  das Feld mitbringt.
- **Parserbruch in mcmap oder Web-RX-Log:** keiner, die Web-Seite liest
  `charBuffer_aprs()` (`src/loop_functions.cpp:3120`), nicht die Serial-Zeile.
