# PR gegen icssw-org/MeshCom-Firmware `dev`: Version 4.40a

Titel-Vorschlag: `feat: 4.40a - Store-and-Forward, Nachbarschaftsmatrix statt MHeard/Pfadtabelle, serielle Konfiguration, Fehlerkorrekturen`

Basis: `upstream/dev` 1d4f5250 (4.35v). Version: 4.40a (`SOURCE_VERSION "4.40"`, Untertyp `a`; das Versionsfeld
auf der Luft bleibt 5 Zeichen). Umfang: nur Firmware (`src/`, `lib/`, `variants/`, `config/`, `platformio.ini`,
`safeboot*.bin`). Keine Tests, keine Doku, keine Werkzeuge.

## Kurzfassung

- Neu: Store-and-Forward (Mailbox-Knoten) fuer Direktnachrichten, nur ESP32-S3 und nRF52840, standardmaessig aus.
- Neu: Nachbarschaftsmatrix (NBR). Sie ersetzt die bisherige MHeard-Liste und Pfadtabelle als Datenquelle fuer
  `--mheard`, `--path`, Web, BLE und T-Deck. Das ist die groesste sichtbare Aenderung (Abschnitt 3).
- Neu: 19 serielle/USB-Kommandos, Kurzformen, tabellengetriebene Toggle-Behandlung.
- Behoben: Fehler in BLE, Ethernet, TX-Ring, Web und Anzeige (Abschnitt "Behobene Fehler").
- Aufgeraeumt: Einstellungen, Zaehler, BLE- und UDP-Pfade in eigene Module; ungenutzte Fonts, Plattform-Stubs und
  auskommentierter Testcode entfernt.

## Was wurde geaendert

### 1. Store-and-Forward (S&F) fuer Direktnachrichten

Dateien: `src/msgstore.cpp`, `src/msgstore_api.h`, `src/msgstore_glue.cpp`, `src/msgstore_settings.{h,cpp}`,
`src/sto_notice.{h,cpp}`, `src/dm_dedup.{h,cpp}`, `src/reack_limiter.{h,cpp}`, `src/dm_stats.{h,cpp}`, Haken in
`src/lora_functions.cpp` und `src/loop_functions.cpp`, Web-Seite `/?page=mailbox`.

- Problem: Eine Direktnachricht (DM) an eine gerade nicht erreichbare Station geht verloren.
- Absenderseite unveraendert gegenueber upstream: `MAX_RETRANSMIT` 3 im Abstand von 40 s, also 4 Sendungen in ca.
  2 Minuten, neue `msg_id` je Kopie (`pnRetryId`) bei stabiler `{NNN`. Neu sind nur eine Rueckmeldung 0x03
  "fehlgeschlagen" an die App beim Aufgeben und zusaetzliche Zaehler (`ack=`, `rtt=`, `att=`).
- Empfaenger: doppelte DMs werden verworfen (Dedup ueber Quelle und NNN), aber erneut quittiert (Re-ACK, begrenzt durch
  `reack_limiter`), damit ein verlorener ACK keine Dauerschleife ausloest.
- Mailbox-Knoten (Store-Node): speichert eine DM, deren Ziel er nicht erreicht (keine Hop-Pruefung beim Speichern).
  Sobald er das Ziel wieder direkt hoert (Pfad ohne Komma), liefert er mit einer 9-stufigen Leiter bei
  `max_hop 0` aus: Sendungen bei 0/40/80/180/220/260/360/400/440 s (insgesamt 7 min 20 s), danach 1 h Pause je
  Eintrag. Jede Sendung hat eine neue `msg_id` (`millis()`), `{NNN` bleibt. Zufallsverzoegerung 5-60 s; hoert er eine
  Auslieferung eines anderen Knotens fuer dieselbe (Quelle, NNN), bricht er ab; ein ACK beendet die Leiter. Die
  Mailbox liegt nur im RAM, ein Neustart verwirft sie.
- Grenzen: Aktion hoechstens alle 30 s, hoechstens 20 Aktionen pro Stunde und Knoten, keine Auslieferung bei QRS/QRT
  oder mehr als 25 % Kanalauslastung (5 min). Nutzlast bis 160 Byte.
- Rueckmeldung an den Absender: Status 0x04 (gehalten, Halter angehaengt) und optional der Text `:stoNNN <Ziel>`
  (ratenbegrenzt, `--storenotice`, Standard an).
- Modi (`--store`): `off` (Standard) | `own` (Ziele mit demselben Basis-Rufzeichen wie der Knoten) | `list`
  (Rufzeichen aus `--storecall`, max. 16) | `heard` (Ziel in den letzten 12 h gehoert).
- Hardware: Boards mit `ENABLE_MSGSTORE` (alle ESP32-S3-Envs, RAK4631, T114, T-Echo). Sonst nicht eingebunden.
- Persistenz: eigener Satz (NVS auf ESP32, `/msgstore.cfg` im LittleFS auf nRF52), nicht in `s_meshcom_settings`.

### 2. Nachbarschaftsmatrix (NBR)

Dateien: `src/nbr_matrix.{h,cpp}`, `src/nbr_mask.h`, `src/nbr_views.{h,cpp}`, `src/topo_ui.{h,cpp}`, Anbindung in
`src/lora_functions.cpp` und `src/txring_functions.cpp`, Anzeige `/?page=neighbours`, Kommando `--neighbours`.

- Berechnet eine lokale Zwei-Hop-Matrix "Station Y hoert Station X" aus den Pfadfeldern empfangener Frames (12-h-Fenster)
  und HEY-Signalberichten. Die Matrix selbst wird nicht gesendet.
- Pro weiterzuleitendem Frame (Quelle, NNN): Bedarf(F) = direkte Nachbarn, die das Frame noch nicht haben; Allein(F) =
  Nachbarn, die nur ich erreiche. Fall A (Allein nicht leer): ich relaye mit Prioritaet, nie abbrechen. Fall B:
  andere decken ab, ich relaye mit Backoff und breche ab, sobald die Abdeckung gehoert wird (abdeckungs-, nicht
  zaehlerbasiert). Die Abbruch-Pruefung liest den TX-Ring jetzt unter dem Ring-Lock erneut, bevor sie einen Slot
  freigibt.
- Textframes (`:`) und damit auch vom Server eingespeiste Frames fuettern nur den eigenen Schritt (ME), keine
  Pfadkanten; so entstehen keine Phantom-Nachbarn.
- Kein neues Luftformat fuer die Entscheidung: aeltere Firmware flutet weiter wie bisher. Optionaler HN-Bericht
  (`--nbrreport`, Standard `auto`): ein HEY-Frame an "HN" je 15 min bei `max_hop 0`; Stock-Firmware verwirft ihn, weil
  "HN" die Rufzeichen-Pruefung nicht besteht.
- Speicher: ESP32-S3 und nRF52840 128 Zeilen / 512 Kanten, `NbrMatrix` 13.992 B, TX-Ring-Masken 660 B; klassischer
  ESP32 64 Zeilen / 256 Kanten, 5.592 B, 340 B.
- Schalter liegen in `node_sset4` 0x0100-0x2000 und damit ausserhalb der KISS/TCP-Bits 0x0010-0x0080.

### 3. MHeard und Pfadtabelle durch die NBR ersetzt (sichtbare Verhaltensaenderung)

`src/mheard_functions.{cpp,h}` und `src/mheard_throttle.h` entfallen, `src/mh_phone.{h,cpp}` ist die Nachfolge.

- `--mheard` (seriell): statt der ASCII-Tabelle Zeilen `[MH] key=value`, neueste zuerst; Spalten pl/mesh und numerische
  HW-ID entfallen.
- `--path`, Web-Pfadseite, T-Deck-Pfad-Tab: zeigen Hop-Zahl und Nachbarn aus der Matrix statt der vollstaendigen
  Relay-Pfadzeichenkette.
- Web-MHeard-Seite: SNR als 8-Frame-Mittel, andere Beschriftung, zusaetzliche Felder.
- BLE-MHeard-Rahmen zur App: neueste zuerst, hoechstens ein Live-Rahmen je Station und Minute, sieben neue Schluessel
  (AGE HM ROLE EX NB GW VIA) am Ende angehaengt, alte 13 Schluessel und deren Reihenfolge unveraendert. Fuer direkte
  Stationen ohne Detail-Platz (mehr als 64 / 48 direkte Nachbarn auf S3, RAK / klassischem ESP32) fehlen RSSI, SNR,
  PLT, MOD, PL und MESH im Rahmen statt Platzhalterwerten; DIST bleibt immer numerisch. Die Stock-App toleriert
  unbekannte Schluessel und fehlende Werte (geprueft gegen den App-Quelltext).
- Auf der Luft aendert sich der Wert, nicht das Format, der Nachbarzahl in HEY `R<n>`, Position `/N` und HEY-Signalbericht:
  vorher `getMheardCount()` (alle in 60 min gehoerten Stationen), jetzt `NCNT` (zweiseitige Nachbarn, hoechstens 99,
  kleiner oder gleich dem alten Wert). Server und Karte zeigen daher fuer neue Knoten systematisch kleinere Zahlen.
- Persistenz: `/mheard.dat` und `/mhpath.dat` werden beim ersten Start geloescht (Historie geht einmalig verloren),
  Speicherintervall 30 s -> 10 min, T-Deck Pro schreibt neu `/topo.dat` auf die SD.
- Kapazitaet mit voller Detailzeile: S3/RAK 80 -> 64, klassischer ESP32 30 -> 48.

### 4. Neue serielle/USB-Kommandos (19)

Standardwerte in der Spalte "Std.". Nur `--nbrdebug`, `--nbrrelay`, `--nbrsym`, `--nbrreport` sind Zeilen der Tabelle
`COMMAND_TOGGLES` (`src/command_functions.cpp`, 80 Zeilen insgesamt, Typen in `src/command_toggles.h`); alle anderen
sind handgeschriebene Stufen der Kommandokette.

| Kommando         | Argumente          | Std. | Wirkung                                                                    | Speichern | Einschraenkung                                       |
| ---------------- | ------------------ | ---- | -------------------------------------------------------------------------- | --------- | ---------------------------------------------------- |
| `--neighbours`   | -                  | -    | Nachbarmatrix anzeigen (Kurzform `--nbr`)                                  | nein      |                                                      |
| `--nbrreset`     | -                  | -    | Matrix leeren                                                              | nein      |                                                      |
| `--nbrcheck`     | -                  | -    | Konsistenzpruefung der Matrix                                              | nein      |                                                      |
| `--nbrdebug`     | on/off             | off  | Debugausgabe aller NBR-Entscheidungen                                      | ja        |                                                      |
| `--nbrrelay`     | off/count/on       | off  | Relay-Entscheidung: aus / nur protokollieren / aktiv abbrechen             | ja        |                                                      |
| `--nbrsym`       | on/off             | on   | Symmetrie-Annahme (SNR >= -16 dB), wenn keine direkte Beobachtung vorliegt | ja        |                                                      |
| `--nbrreport`    | off/auto/on        | auto | HN-Nachbarbericht (`auto`: nur wenn Mesh und Gateway aus)                  | ja        |                                                      |
| `--store`        | off/own/list/heard | off  | Mailbox-Modus; ohne Wert: Status                                           | ja        | `ENABLE_MSGSTORE`                                    |
| `--storecall`    | Liste/none         | leer | Rufzeichen fuer Modus `list` (max. 16)                                     | ja        | `ENABLE_MSGSTORE`                                    |
| `--storeslots`   | 1-50               | 50   | Mailbox-Plaetze                                                            | ja        | `ENABLE_MSGSTORE`                                    |
| `--storetime`    | 1-168              | 24   | Haltezeit in Stunden                                                       | ja        | `ENABLE_MSGSTORE`                                    |
| `--storenotice`  | on/off             | on   | `:sto`-Hinweis an den Absender                                             | ja        | `ENABLE_MSGSTORE`                                    |
| `--mbox`         | -                  | -    | Mailbox-Inhalt anzeigen                                                    | nein      | `ENABLE_MSGSTORE`                                    |
| `--msgid`        | -                  | -    | Nachrichtenzaehler anzeigen                                                | nein      |                                                      |
| `--airgap`       | on/off/show        | off  | Pruefstand: Empfang verwerfen, Senden verweigern                           | nein      |                                                      |
| `--battprobe`    | [n] 1-10           | 1    | Batterie-ADC-Messung (ca. 7,5 s je Zyklus)                                 | nein      | `INSTRUMENT_ENABLED`, Heltec mit geschaltetem Teiler |
| `--dumpsettings` | -                  | -    | Rohdump der Einstellungen                                                  | nein      | nur nRF52                                            |
| `--keylock`      | on/off             | off  | Tastensperre (SYM+K)                                                       | ja        | T-Deck, T-Deck Plus                                  |
| `--ethmtu`       | 1280-1500          | 1500 | Ethernet-MTU (kleiner als 1500 begrenzt die TCP-MSS des Webservers)        | ja        | RAK4631                                              |

Weitere Aenderungen: `--help` ist nach Gruppen gegliedert und hinter den Guards der jeweiligen Handler; ein Teil der
Kommandos hat weiterhin keinen Hilfetext. Exakte Token statt Praefix-Treffer in der Toggle-Tabelle. Entfernt wurde
auskommentierter Code der Testkommandos `--compress` und `--softser test|test0|xml`.

### 5. Strukturelle Aenderungen

- `s_meshcom_settings` hat eine einzige Definition in `src/meshcom_settings.h`; Schema und Flash-Pfade in
  `src/settings_schema.*`, `src/settings_store.*` (ESP32: NVS mit unveraenderten Schluesselnamen, nRF52: Datei).
  Die Wertebereiche des Schemas sind die Obergrenzen der Setter (u. a. `node_postime` bis 86400 s, `node_alt` bis
  40000, `node_gpsdebug` bis 3, `--postime` begrenzt jetzt auf einen Tag).
- Zaehler (`node_msgid` u. a.) getrennt in `src/counters_store.h`; `finalizeAndSendAPRS()` ersetzt die Einzelkopien
  des Sende-Abschlusses; `save_msgid()` entfaellt.
- BLE: Phone-Frame-Zerlegung und Drain in `src/ble_phone_*.h`; UDP/Gateway in `src/udp_frame*`, `src/gateway_service*`.
- Datenbank-/Hilfsmodule: `src/country_profile.*`, `src/radio_units.*`, `src/loop_scheduler.*`, `src/uptime_min.h`,
  `src/batt_pipeline.h`, `src/ui_common/*`.

### 6. Build-Aenderungen (fuer den Reviewer sichtbar)

- RAK4631 baut mit `-Os` statt `-Ofast` (`variants/wiscore_rak4631/platformio.ini`, `build_unflags = -Ofast`, `-Os`):
  Flash 96,1 % -> ca. 70 %, RAM unveraendert; beendet dort `-ffast-math`, damit funktionieren `isnan()`-Pruefungen in
  upstream-Code (z. B. `bmx280.cpp`, `onewire_functions.cpp`) wieder. T114 und T-Echo bleiben auf `-Ofast`.
- Klassische T-Beam-Envs: PSRAM-Build-Flags entfernt. E22_XML: KISS/TCP wieder einkompiliert, `MC_CAPTURE=0` ->
  `MC_DIAG=0`, `tinyxml2` als eingebettete Kopie unter `lib/tinyxml2`.
- `t5_epaper` baut jetzt (neue `configuration.h`, bq27220-Quellen, `lib_deps`); T-Deck-Pro-LVGL-Speicher im PSRAM.
- Neue Basis-Sektionen `[esp32_s3]`/`[esp32_classic]` mit gemeinsamem `upload_command` ueber `$BUILD_DIR`
  (Adressen, Dateien und `--port` unveraendert).
- Neue Batterie-Pipeline fuer alle Boards (`src/batt_pipeline.h`, andere Abtastung). `OneWire_GPIO` 99 -> -1 bei 8 Boards.
- `settings_schema.cpp` ist in den Safeboot-Envs im Quellfilter (die Flash-Pfade benutzen das Schema).
- `default_envs`, Partitionstabellen und `-Werror` sind unveraendert; die Workflow-Datei `meshcom-ci.yml` braucht keine
  Aenderung (gleiche Envs, gleiche Artefaktpfade).
- `safeboot.bin` und `safeboot-s3.bin` sind aus diesem Stand neu gebaut (Eingaben `esp32_flash.*` und
  `settings_schema.cpp` haben sich geaendert).

### 7. Entfernt

- 43 ungenutzte Adafruit-GFX-Fonts unter `src/Fonts/`, 11 AVR-/SAMD-/ESP8266-Dateien unter `src/Platforms/`,
  `src/nrf52/glcdfont.c` (Duplikat), `src/t-deck-pro/ui_scr_mrg.c` (ersetzt durch `src/ui_common/scr_mgr.c`),
  `src/idf_component.yml.orig`.

## Behobene Fehler

Nur Fehler, die in `upstream/dev` 1d4f5250 noch vorhanden sind.

- BLE-N1/N2/N3 (nRF52): ein abgelehntes Notify verlor das Frame (upstream verwirft das Frame unabhaengig vom
  Sendeergebnis); jetzt Rueckgabe `BLE_SEND_*` und Wiederholung.
- BLE-N4: Telefon mit falscher PIN-Pruefsumme wird sofort getrennt (upstream prueft nur im Rx-Callback).
- nRF52: Schreiben der Einstellungen ueber BLE kopierte das ganze Telefon-Abbild inklusive veraltetem `node_msgid`
  ueber die laufenden Einstellungen; der Zaehler lief zurueck.
- P13: Pong an das eigene Rufzeichen erreicht den BLE-Client.
- P14: `{ping}` wurde aus `sendMessage()` wiederholt gesendet, jetzt einmal.
- P15: eigene Nachrichten ohne Wiederholung behalten ihre TX-Ring-Prioritaet (upstream behandelt sie als Relay).
- #1182: T-Tracker: TFT-Hintergrundlicht folgt "Display aus".
- #1183 (opt-in): W5100S (RAK4631) begrenzt die TCP-MSS nicht; `--ethmtu` unter 1500 setzt sie fuer den Webserver.
  Standard 1500 laesst das Verhalten unveraendert.
- #1173 (Rest): der Nachrichtenzaehler der Weboberflaeche zaehlt UTF-8-Bytes.
- Web: tote Schalter der Info-Seite entfernt (die Schalter sind jetzt gegen nicht gebaute Funktionen abgesichert).

## Hinweise fuer den Reviewer

- Der PR ist gross und Teil der Versionssprung-Entscheidung auf 4.40a. Der Umfang ist ueber Abschnitt 1-7 gegliedert.
- Kompatibilitaet auf der Luft: kein Formatbruch. Aenderungen: Nachbarzahl `R<n>`/`/N` (Abschnitt 3); Mailbox-Sendungen
  tragen je Stufe eine neue `msg_id` (`millis()`): ein Stock-Ziel, das den ACK verpasst hat, zeigt die Nachricht erneut
  an, und jedes Gateway laedt jede Stufe hoch (nur mit einem Store-Node, `--store` ist standardmaessig aus).
  In den Dedup-Ringen der Relays erhoeht die DM-Leiter die Zahl unterschiedlicher IDs (Fenster 39-48 min gemessen).
- Mobile App: Statuswerte 0x03 (fehlgeschlagen, bei jedem Aufgeben einer eigenen DM) und 0x04 (gehalten) kennt die App
  nicht; sie laesst die Nachricht unveraendert (zeigt nach 6 min "unbestaetigt"), eine zugestellte DM wird nie
  herabgestuft. Fuer eine eigene Anzeige muesste die App ack=3/4 ergaenzen.
- Rueckschritt (Downgrade) nRF52 auf upstream: das alte Einstellungsblatt bleibt auf dem Stand vor dem Update stehen,
  der Knoten startet mit Standardwerten bzw. altem `node_msgid`. Vorher Konfiguration exportieren.
- Offen bekannt: zwei Mailbox-Knoten, die sich nicht hoeren, liefern beide aus (bis zu doppelte Sendezeit, begrenzt
  durch 20 Aktionen/h und 1 h Pause je Eintrag). Ein Knoten, der mich hoert und den ich nie hoere, erscheint nicht in
  der Matrix. Restfenster in der Mailbox-Freigabe (`gen`-Pruefung) und ungesperrte Dedup-Tabellen auf nRF52-Gateways:
  folgenlos bis auf einmal doppelte Anzeige/ACK, keine Speicherverletzung.
- Version: `SOURCE_VERSION` 4.40, Untertyp `a`; `shortVERSION()` liefert 40, die App vergleicht numerisch (4.40a ist
  neuer als 4.35v).

## Getestet

- Gebaut: alle 32 Board-Envs der Konfiguration (30 aus `default_envs` plus `t5_epaper` und
  `vision-master-e213-preview`) und beide Safeboot-Envs, alle gruen. `esp32-external-radio`
  bricht wie upstream bewusst am `#error` fuer fehlendes Host/Port ab.
- Upstreams eigene Host-Tests (`native_safeboot`, `native_pnretry`, `native_extradio`) laufen auf dem PR-Zweig.
- Lokal (nicht Teil des PR): 48 Host-Umgebungen gruen, darunter NBR-Matrix, -Replay, -Views, Toggle-Tabelle,
  Settings-Round-trip (neu: gesetzlich einstellbare Werte ueberleben Speichern/Laden), MHeard-Rahmen (neu: Station ohne
  Detail ohne Platzhalter), PN-Retry, Safeboot-Zustandsmaschine.
- Hardware (Pruefstand, 2 dBm, eigene Rufzeichen): RAK4631 (nRF52840) und Heltec V3 (ESP32-S3) mit diesem Stand
  (4.40a, Build 01.10.2026) geflasht; Rufzeichen, Sendeleistung und Webserver-Einstellungen blieben beim Update
  erhalten. Auf dem RAK ueberlebt `--postime 3600` den Neustart (vorher 1440). `--mheard`, `--path`, `--neighbours`,
  `--nbrcheck`, `--store` und `--msgid` liefern auf beiden Knoten im Live-Mesh plausible Werte (RAK hoert DK5EN-1 und
  DK5EN-98 direkt, die Matrix fuellt sich). T-Beam und T-Deck waren nicht erreichbar und sind nicht hardwaregetestet.
  Ein Mehrtages-Soak mit diesem Stand liegt noch nicht vor.
