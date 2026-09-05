# Release Notes -- MeshCom Firmware v4.35s

Firmware `4.35s`, `FLASH_VERSION 20260905`, `FLASH_STRUCT_VERSION 20260724`
(`src/configuration_global.h`).
Aeltere Eintraege bis einschliesslich 2026-03-22 stehen im Archiv
[`docs/release_lora_trx.md`](docs/release_lora_trx.md).

---

## Stability-Release v4.35s.09.05 (2026-09-05)

Ab diesem Release traegt der Tag kein `-stability`-Suffix mehr; die Linie und
ihre Regeln bleiben gleich. Dreizehn Aenderungen des Forks gegenueber
`v4.35s.09.03-stability`, Changelog-Punkte 179 bis 191. `FLASH_VERSION`
20260905, `FLASH_STRUCT_VERSION` unveraendert 20260724 -- die Einstellungen
der Knoten bleiben erhalten. Gates: 591 native Testfaelle in 12
Host-Umgebungen, alle 32 Release-Umgebungen gebaut.

### Was dazugekommen ist

- **Echo-Sperre fuer die eigene Back-Pressure-Formulierung (BP-11, Punkt
  179).** Ein angeschlossener Client (App, oder ein zweiter Knoten ueber
  EXTUDP) konnte die `QRT NOT SENT - <text>`-Quittung des Knotens als neue
  Nachricht zurueckreichen; der Knoten lehnte erneut ab, stellte erneut das
  Praefix voran, und nach dem Leerlaufen des Rings ging der Stapel in die
  Luft. Feldbeleg: fuenf Praefixe tief, neun Frames in drei Sekunden. Der
  Knoten erkennt seine eigene Formulierung beim Einreichen exakt
  (`bpIsOwnWording()`, `src/backpressure.h`) und verwirft sie still. Die
  Veroeffentlichung war vom 04.09. bis 05.09. auf Betreiberentscheidung
  zurueckgehalten und wurde fuer dieses Release freigegeben. Beleg in
  `docs/node-msg.md`.
- **RAK4631 sendet nach Flash-Reset wieder mit 22 dBm (Punkt 180, upstream
  #1132).** Seit 4.35p ist die Markierung "Leistung nicht gesetzt" -20 statt
  0; der nRF52-Bootpfad hat sie nie normalisiert, `getPower()` klemmte -20
  auf 2 dBm, die App zeigte -20. `resolve_tx_power()` in
  `src/settings_sanitize` bildet beide Markierungen auf die Board-Vorgabe
  ab; `nrf52setup()` wendet sie vor der Funkkonfiguration an,
  `sendNodeSetting()` meldet den wirksamen Wert. Native Test mit sechs
  Faellen.
- **T-Deck, alle mit Bench-Beleg auf DK5EN-14 (Punkte 181 bis 185):**
  Trackball-Taste liefert einen LVGL-Click pro Druck statt zwei (TD-13);
  Send und Save Setting wechseln ohne Animation zurueck auf den
  Nachrichten-Tab, die halb abgebrochene Animation liess den Bildschirm auf
  einem geteilten Frame stehen (TD-12); der Karten-Tab komponiert die
  SD-Karte einmal statt zweimal, 718 ms statt 1337 ms Loop-Luecke (TD-14);
  das `msg_roll`-Szenario belegt, dass der Nachrichten-Tab keine Totzeit
  hat, die gemessenen Totzeiten sind SD-Karten-Rekompositionen (TD-14,
  TD-09).
- **ESP32-S3 an nativem USB blockiert die Hauptschleife nicht mehr, wenn der
  USB-Host weg ist (CDC-01, Punkt 184).** Der HWCDC des Arduino-Cores setzt
  nach dem ersten Host-Read ein 100-ms-Timeout und senkt es nie wieder; nach
  dem Ziehen des Kabels blockierte jeder Print, der nicht in den
  256-Byte-Ring passte. Timeout 0 (verwerfen statt warten), 4-kB-Ring.
  Beleg: 7 Luecken bis 1,8 s in 44 s ohne, keine mit der Aenderung.
- **Safeboot-OTA schliesst fail-closed ab (TM-49, Punkt 186).** Ein Upload,
  dessen Verbindung vor dem letzten Frame abbrach, erreichte den
  Abschluss-Handler ohne Fehlerflag und schaltete die Boot-Partition auf ein
  halb geschriebenes Image um. `_ota_image_valid` wird an genau einer Stelle
  gesetzt, nach `Update.end(true)` mit MD5-Pruefung; beide Handler koppeln
  Callback, Reboot und HTTP 200 daran, sonst 400 `incomplete_upload` und
  Rueckfall auf den 180-s-Timer des Safeboot.
- **Extern-UDP `tele`-Datagramm relayter Knoten (TLM-04, Punkt 187):** `qfe`
  trug die `/F=`-Druckhoehe in Metern statt des `/P=`-Stationsdrucks.
  `qfe` ist jetzt der Druck, die Hoehe steht unter dem neuen Schluessel
  `pressure_alt` (nur lora-Form). Builder in `src/extern_tele_json.h`,
  nativ getestet.
- **Analogeingang mit ungesetztem GPIO (ADC-01, Punkt 188):** kein
  `Pin 99 is not ADC pin!`-Sturm mehr, Abtastung pausiert, `--info` und
  Web-Status sagen `GPIO not set, measurement paused`. Die gespeicherte
  Einstellung wird nie hinter dem Betreiber geaendert.
- **Web-GUI (Punkte 189, 190):** Queue-Panel auf der RX-Log-Seite (TX-Ring
  je Prioritaet, Back-Pressure-Zustand, Dedup-Fenster, letztes
  STAT-Fenster; Flash +7 kB, RAM unveraendert). Die Nachrichten-Seite liest
  den ganzen BLE-Ring statt des Telefon-Fensters -- mit verbundener App war
  sie bisher immer leer --, haelt die Historie im Browser (Limit 200) und
  filtert ueber Tabs (Alle, `*`, je Gruppe, DM).
- **Werkzeug (MEM-03, Punkt 191):** der Speicher-Waechter prueft jetzt
  `iram0_0_seg` und `dram0_0_seg` aus der Map-Datei; bisher sah er nur DRAM
  und die PSRAM-inklusive Summenzeile, deshalb blieb der IRAM-Ueberlauf von
  PR #1114 unentdeckt.

### Was fuer dieses Release auf Hardware geprueft wurde

- **T-Deck Plus (DK5EN-14, Bench):** TD-13 5 von 5 Clicks korrekt mit, 5 von
  5 falsch ohne die Aenderung; TD-14 `map_tab_pick` ein Rebuild 294x182,
  718 ms; CDC-01 Loop-Luecken-Zaehler ueber den Port-Open-Reset getragen;
  TD-12 Tab-Wechsel nach Send/Save friert nicht mehr.
- **RAK4631 (DK5EN-90, Bench, Build `dfc02801`):** vor der Aenderung
  `TXPWR 2 dBm`, nach `--cleanflash` und Reboot `RF_POWER: 22 dBm` im
  Bootlog und `TXPWR 22 dBm` in `--info`; Sweep `--txpower 15/10/5/2` exakt
  zurueckgelesen, `1`, `0`, `-20` abgelehnt, gespeicherte 2 dBm ueberleben
  den Reboot.
- **Heltec V3 (DK5EN-98, Bench und seit 04.09. als Gateway im Betrieb):**
  Echo-Sperre vier von fuenf Bench-Faellen.
- **Bauen:** alle 32 Release-Umgebungen, 591 native Testfaelle gruen.

### Was ausdruecklich NICHT geprueft wurde

- Der Ring-Flut-Fall der Echo-Sperre (fuenfter Bench-Fall) auf DK5EN-14.
- Der fail-closed-Pfad des Safeboot auf einem 4-MB-Board mit einem
  abgebrochenen Upload.
- Die Web-GUI-Aenderungen als gemessener Bench-Arm; sie wurden waehrend der
  Entwicklung interaktiv benutzt, der Zehn-Minuten-Ringueberlauf aus dem
  Plan ist nicht gelaufen.
- T-Beam v1.2 und alle nicht genannten Boards: nur gebaut.
- Alle aus 09.03 uebernommenen offenen Punkte (GPS-07, `--setlog` auf
  Hardware, GPS-Vergleichsarme, `--postime 0`, TD-15, MEM-04-Risiko).

---

## Stability-Release v4.35s.09.03-stability (2026-09-03)

Wartungs-Release. Der **gesamte Firmware-Unterschied zu
`v4.35p.09.02-stability` ist upstreams eigener `4.35s`-Schnitt**
(`c908a4dd`, ueber PR #1126 in `dev`): zwei Zeilen Versionskennung und eine
Korrektur an `--postime`. Aus diesem Fork ist in diesem Schnitt keine einzige
Code-Zeile dazugekommen -- die Aenderungen des Forks seit dem letzten Release
sind Dokumentation und Werkzeug-Metadaten.

### Was upstream geaendert hat

- **`--postime` hat bisher jeden brauchbaren Wert verworfen.** Der Befehl hob
  Werte unter 300 s auf 300 s an und setzte im `else`-Zweig jeden Wert **ab**
  300 s auf `0`, was auf den einkompilierten Vorgabewert durchfiel. Damit war
  ueber `--postime` gar kein Intervall einstellbar. Upstream hat den
  `else`-Zweig entfernt; ein Wert ab 300 s wird jetzt uebernommen und wird zu
  `posinfo_interval`. (Changelog-Punkt 177.)
- **Versionskennung `p` -> `s`**: `SOURCE_VERSION_SUB` und
  `SOURCE_VERSION_WEB_SUB` melden `4.35s` in Banner, `--info`, Weboberflaeche
  und Positionsbake. (Changelog-Punkt 178.)

### Was der Fork dazu geaendert hat

Nichts an der Firmware. Organisatorisch: der Arbeitsbranch hiess
`v4.35p_prio` und war damit nach dem Versionsbuchstaben von upstream benannt
-- er veraltete bei jedem upstream-Schnitt. Er ist auf den versionsneutralen
Namen `fork-main` umgestellt, der alte Remote-Branch ist geloescht, der letzte
`4.35p`-Stand liegt unter dem Tag `archive/v4.35p_prio-20260903`. Das
Tag-Schema der Release-Prozedur liest die Version jetzt aus
`configuration_global.h`, statt `4.35p` fest verdrahtet zu haben.

### Bekannte Einschraenkung, bewusst uebernommen

`--postime 0` schaltet die periodische Positionsbake nicht mehr ab: `0` ist
kleiner als 300 und wird jetzt auf 300 s angehoben. Vorher war der Befehl in
die andere Richtung kaputt. Der Fork patcht das nicht selbst, damit die beiden
Baeume nicht auseinanderlaufen -- das gehoert als eigener PR nach upstream.

### Was fuer dieses Release auf Hardware geprueft wurde

Nichts. Es gab in diesem Schnitt keinen Hardware-Lauf. Das ist vertretbar,
weil der Quelltext ausserhalb der beiden upstream-Hunks bytegleich zu
`v4.35p.09.02-stability` ist -- dessen Bank- und Feldergebnisse (T-Deck Plus
DK5EN-14, OE5HWN T-Deck Plus und T-Beam Supreme je eine Stunde GPS) gelten
unveraendert weiter.

### Was ausdruecklich NICHT geprueft wurde

- **Der `--postime`-Fix selbst.** Er ist gelesen und verstanden, aber auf
  keinem Knoten gegen eine tatsaechlich veraenderte Bakenperiode gemessen.
- Alle offenen Punkte aus `v4.35p.09.02-stability` bleiben offen: der
  `--setlog`-Banklauf (RAK-90 + Heltec-93, 30 min), die GPS-Zwei-Stunden-Arme
  A/B/C, der T-Deck-Pin-Fallback auf Hardware, TM-49 (Safeboot-OTA-Abschluss
  nach Abriss), das BLE-`blelen + 2`-Ueberlaufrisiko.
- **GPS-07** (neu im Backlog, Nachtaufzeichnung 2026-09-03): der
  Kalman-Filter fuer die Hoehe setzt sich nachts zehnmal auf rohe Ausreisser
  neu auf -- die Re-Seed-Schranke ist zu klein bemessen. In der Wiedergabe
  behoben durch `RESEED_N 60` bzw. ein 30-m-Gate; beides ist in diesem
  Release **nicht** enthalten.

---

## Stability-Release v4.35p.09.02-stability (2026-09-02)

Drei Teilsysteme aus parallelen Worktrees, jedes einzeln reviewt
(`/fable-review`, Finder-Faecher plus adversarialer Verifizierer) und heute
Abend in dieser Reihenfolge gemerged: `--setlog on`-Instrumentierung
(Item 173), GPS-Hoehe/QNH-Pfad (Item 174), T-Deck Tasten-Auto-Repeat
(Item 175). Dazu Code Quality 2.0 mit Detektoren und der Reset-Grund im
ESP32-Bootbanner (170-172) und der Upstream-`dev`-Sync #1124 (176).
`FLASH_VERSION` steigt auf 20260902. Native-Gate 568/568 in 12 Host-Envs,
102 Tool-Tests.

### Was dazugekommen ist

- **GPS-Pfad (GPS-01..04, Feldmeldung OE5HWN-14).** Die GPS-UART wird in
  jedem Schleifendurchlauf geleert statt alle 3 s; vorher gingen ~165 Byte
  NMEA je Zyklus verloren und etwa jeder 256. zusammengesetzte Satz kam als
  "Fix" mit `lon:0.000000` oder `Date: 2015.14.00` durch die Pruefsumme.
  Ein Plausibilitaetsgatter verwirft Null-Insel, unmoegliche Kalenderdaten
  und Uhrzeiten, Hoehen ausserhalb -500..10000 m, bevor sie Settings,
  Beacon oder Systemuhr erreichen. Die gebeaconte Hoehe ist ein
  Kalman-Schaetzwert (Zeitkonstante ~7 min, dt-skaliertes Prozessrauschen
  fuer den 1-s-Takt auf nRF52), TRACK-Modus umgeht ihn; `--setalt` seedet
  den Filter und lehnt Werte ausserhalb 0..40000 m ab statt auf 0 zu
  klemmen. Die QNH-Referenzhoehe wird bei Konvergenz und bei
  `--setalt`/`--setpress` nachgezogen, nicht mehr auf den ersten Fix nach
  dem Boot festgenagelt. Feldlogs OE5HWN (T-Deck Plus, T-Beam Supreme, je
  eine Stunde): 2375 Auswertungen ohne eine korrupte Stichprobe,
  Konvergenz nach 88 Stichproben wie modelliert, QNH-Relatch nach Reboot.
- **GPS-06: T-Deck ohne Plus, Pin-Fallback.** Seit Upstream `a672d18b`
  (v4.35p) empfaengt der T-Deck auf GPIO44 und sendet auf GPIO43 (LilyGo-
  und Plus-Belegung); bis 4.35d war es per `SoftwareSerial(43, 44)`
  umgekehrt, selbst verdrahtete Module nach alter Belegung waren seither
  stumm (Feldlog: 4.35d Fix mit 7 Satelliten, 4.35p "keine gueltige
  NMEA-Sequenz auf 8 Baudraten"). `detectBaudrate()` scannt bei Fehlschlag
  einmal auf der alten Belegung, merkt sich die wirksamen Pins fuer alle
  spaeteren `begin()` und bittet im Log ums Umverdrahten. Nur
  `variants/t_deck` definiert den Fallback.
- **`--setlog on`: sieben Zeilenarten (SL-01..SL-07).** RX-Zeile mit
  `RSSI:`/`SNR:`/`DUP:`/`OWN:`/`t=`, neu `RLY` (Relay-Entscheidung mit
  Grund), `TX` (eigener Sendevorgang mit Wartezeit, Ringtiefe,
  CAD-Versuchen), `ERR` (RX-Fehler auf beiden Plattformen), `STAT`
  (5-Minuten-Kanalauslastung, Dedup, Ring-Hochwasser, Drops, Heap) und
  `GWI`/`GWU` (Gateway-Inject/-Upload je `msg_id`). Alles haengt nur an
  `bDisplayLog`, unter 64 Byte RAM, keine neuen Puffer; `tools/berglog.py`
  liest die Zeilen (39 Tool-Tests). Review-Fixes S1-S9: ein einziger
  Dedup-Lookup je Frame, Gateway-`newid`, Hop-Maske 0x20, kein `String`
  je Zeile, gemeinsame STAT-Befuellung.
- **T-Deck Tasten-Auto-Repeat (TD-10).** Backspace, Leertaste und
  Buchstaben wiederholen sich beim Halten (400 ms, dann alle 100 ms) ueber
  LVGLs Keypad-Repeat; dafuer wird das Raw-Mode-Fenster der
  Tastatur-Controller-Firmware (I2C 0x03/0x04, 5-Byte-Matrix) fuer die Dauer
  des Haltens geoeffnet und nur scharf geschaltet, wenn der Frame genau die
  Matrixzelle der gedrueckten Taste zeigt. Controller ohne Raw-Mode
  (LilyGo-Firmware vor 2025-06-12) verhalten sich wie bisher; `--info`
  zeigt `...KBD raw-mode yes|no|unknown`. 39 native Faelle, Review-Fixes
  K1-K7.
- **Code Quality 2.0** (`docs/code-quality-2.0.md`, 28 Code- und 16
  Prozessmuster mit Detektoren `CQ2-*` in `tools/code_audit_scan.py`),
  ESP32-Bootbanner mit Reset-Grund (TM-51), zwei Fundstellen der Detektoren
  behoben (`PositionToAPRS()`-`strncat`-Grenze, SD-Map-Timer
  rollover-sicher).
- **Upstream #1124 (OE3LCR):** die ADC-Polaritaetsprobe erkennt einen fest
  anliegenden Spannungsteiler (Wireless Stick V3) als Batteriehardware.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 568/568 in 12 Host-Envs, 102
  Tool-Tests.
- Der gemergte Baum selbst hat nur die Gates gesehen. Alle
  Hardware-Befunde stammen vom Pre-Merge-Testbuild der GPS- und
  Tastatur-Branches (`test-helmut-gps-kbd-20260902`, `df9d407e`); Delta zu
  diesem Release: `--setlog`-Zeilen, T-Deck-Pin-Fallback, `--info`-Zeile,
  Upstream #1124.
- T-Deck Plus DK5EN-14 (Bank): GPS-Lauf 6,5 min, 128 Fixes, 0 Rejects,
  Konvergenz nach 256 s (Modell 249 s); Tastatur `support;1`,
  Haltefenster 0,8-1,2 s fuer Backspace, Leertaste und `d`, wiederholtes
  Loeschen am Display gesehen.
- T-Deck Plus und T-Beam Supreme OE5HWN (Feld, je eine Stunde): 1214 und
  1161 Auswertungen, 0 Rejects, 0 korrupt; Supreme konvergiert nach 88
  Stichproben bei 280 m, Relatch 276 m nach Reboot; QNH 1020,2 hPa gegen
  Flughafen Linz 1018,5 hPa, Rest passt zu einem BMP280-Offset, nicht zu
  einem Hoehenfehler. Die OE5HWN-Tastatur hat keinen Raw-Mode und tippt
  wie bisher.

### Was ausdruecklich NICHT geprueft wurde

- `--setlog on` hat keinen Hardware-Lauf (Welle 3, RAK4631 + Heltec V3,
  30 min, steht aus). Heltec V3, T-Beam und RAK4631 haben diesen Build
  nicht gesehen.
- Die GPS-Zwei-Stunden-Arme A/B/C sind nicht gelaufen; der Nachweis sind
  6,5 min Bank plus zwei Feldstunden.
- Der T-Deck-Pin-Fallback ist auf Hardware unverifiziert; der Build ging an
  OE5HWNs selbst verdrahtetes Geraet, das Ergebnis war vor dem Schnitt
  nicht da.
- Kein nRF52-GPS-Knoten auf der Bank (T114/T-Echo, 1-s-Takt).

## Stability-Release v4.35p.09.01.2-stability (2026-09-01, zweiter Schnitt)

Ersetzt v4.35p.09.01-stability vom Mittag (Release-Objekt geloescht, Tag
bleibt als Marker). `FLASH_VERSION` bleibt 20260901 (gleicher Tag, Praezedenz
08.27.2). Drei Dinge obendrauf: die QRS-Kalibrierung aus dem Feld (Item 167),
der ADC-Multiplikator des Heltec Wireless Stick V3 aus Upstream (Item 168)
und der Upstream-`dev`-Sync mit unserem PlatformIO-Upload-Fix (Item 169).
Native-Gate 489/489 in 12 Host-Envs.

### Was dazugekommen ist

- **QRS erst mit der dritten eigenen Nachricht auf vollem Ring
  (`QRS_MIN_USER_MSGS`).** Feldbefund vom Abend: "QRS -- slow down" war
  auch mit der BP-05-Linie bei Tiefe 5 viel zu empfindlich und kam schon
  bei der ersten Nachricht. Ursache: `txRingDepth()` zaehlt Relay-Frames,
  ACKs und Beacons mit, ein Gateway mit Grundlast 4 gibt dem Absender fuer
  seine erste Nachricht ein QRS fuer einen Stau, den er nicht gebaut hat.
  `onSend()` laeuft genau einmal pro lokal getippter Nachricht und nie fuer
  Relay/ACK/Beacon, also zaehlt die Zustandsmaschine dort ihre eigenen
  Aufrufe: QRS kommt, wenn drei eigene Nachrichten in Folge den Ring bei
  Tiefe >= 5 vorfinden. Faellt der Ring zwischendurch unter 5 (in `onSend()`
  oder im Drain-`poll()`), beginnt die Zaehlung von vorn -- wer so tippt,
  dass die Funke zwischendurch abarbeitet, ist nicht das Problem. QRT und
  QTA bleiben ungegatet, Latch/Hysterese/QRV-Hold unveraendert. Acht
  Zeilen in `src/backpressure.h`, `loop_functions.cpp` unangetastet. Vier
  neue Host-Faelle, sieben bestehende um zwei stille Vorlaeufer ergaenzt,
  der integrierte Burst-Test erwartet QRS jetzt bei Tiefe 7.
- **Heltec Wireless Stick V3 misst seinen Akku wieder** (Upstream PR #1119,
  OE3LCR, Issue #1116). `ADC_MULTIPLIER 4.9245` war vom Vision Master E213
  uebernommen; Referenzmessung ergibt 4.13. Mit dem alten Wert fiel die
  Messung aus dem Plausibilitaetsband von `battDetectUpdate()`, der Node
  meldete "keine Batteriehardware" und liess `/B=` weg.
- **PlatformIO-Upload ohne `upload_port` funktioniert, nRF52-Core-Warnungen
  begraben keine echten mehr** (Upstream PR #1118, von uns).
  `upload_protocol = custom` rief nie die Port-Erkennung auf, `$UPLOAD_PORT`
  blieb leer; mit `esptool` laeuft sie vorher und `upload_command` bleibt
  unser Befehl. `-Wall -Wextra` von `build_flags` nach `build_src_flags`
  (nRF52- und ESP32-Basis): in `build_flags` trafen sie den Adafruit-Core
  und die SoftDevice-Header, ueber 23.000 `-Wunused-parameter` pro
  Vollbuild. Upstreams `v4.35p compile` (#1117) nimmt die Flags aus den
  restlichen Varianten und das `--port` aus den T-Deck-Upload-Kommandos;
  beide Baeume sind synchron.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 489/489 in 12 Host-Envs
  (inkl. der vier neuen `QRS_MIN_USER_MSGS`-Faelle, DJ8MEH-Replay,
  Grundlast-Muster, Flood-Test 13-in-10).
- Heltec V3 dk5en-98 (Live-Gateway) laeuft diesen Build per OTA seit
  21:52 Uhr; die Web-GUI meldet den neuen Build-String, mehr wurde am
  Gateway nicht geprueft.
- Alles aus dem 09.01-Abschnitt darunter gilt fuer den 09.01-Build, der
  sich von diesem nur um eine Konstante in der Zustandsmaschine, den
  Wireless-Stick-Pin-Wert und Build-System-Aenderungen unterscheidet.

### Was ausdruecklich NICHT geprueft wurde

- Kein Burst gegen die neue Drei-Nachrichten-Regel auf Hardware; der
  einzige Feldbefund ist die Beobachtung "QRS bei der ersten Nachricht",
  die den Fix ausgeloest hat. T-Deck Plus, T-Beam und RAK4631 haben diesen
  Build nicht gesehen.
- Der Wireless-Stick-V3-Wert stammt aus OE3LCRs Referenzmessung, kein
  eigenes Board auf dem Tisch.
- Der originale DJ8MEH-Feldvorfall weiterhin nicht End-to-End auf
  Hardware nachprovoziert; TM-49 offen (4-MB-Boards bei marginalem Link
  per USB flashen); Batterie-Nullpunkt am 2S-Pack, INA226 und L76K-GPS
  unveraendert ungeprueft.

## Stability-Release v4.35p.09.01-stability (2026-09-01)

Ersetzt v4.35p.08.31.4-stability (Release-Objekt geloescht, Tag bleibt als
Marker). `FLASH_VERSION` steigt auf 20260901. Inhalt: der Endstand des
Backpressure-Themas -- BP-07 bis BP-10 schliessen die Luecken L1-L4, damit
ist das System komplett und wird ab jetzt als Ganzes berichtet, nicht mehr
in Zwischenschritten --, dazu TM-50 (meshlogger-Zombie-TCP) und der Rueckbau
zweier Reparaturen, deren Upstream-Feature nicht mehr existiert.
Changelog-Items 153-166 fassen im CHANGELOG-stability.md jetzt alles seit
v4.35p.08.31-stability konsolidiert zusammen (die Schnitt-Sektionen
.2/.3/.4 sind dort eingedampft, da keiner dieser Schnitte mehr als
GitHub-Release existiert).

### Was dazugekommen ist

- **BP-07 -- jede abgewiesene oder verworfene Nachricht bekommt eine
  Quittung mit ihrem Text.** Eigenes, nie latchendes Vokabular (`BpNack`),
  ein Rahmen pro verlorener Nachricht: `QRT NOT SENT - <text>` /
  `QTA NOT SENT - <text>`, auf 120 Bytes an UTF-8-Codepoint-Grenze
  gekuerzt, Anfuehrungszeichen/Backslashes/Steuerbytes vor dem
  JSON-Escaping bereinigt. Die Refuse-Pruefung laeuft jetzt NACH dem
  Dekodieren und dem Abstreifen des `{ZIEL}`-Praefixes -- die Quittung
  traegt den echten getippten Text, der ~60-Zeilen-Zweitparser
  (`bpPeekDst`) ist geloescht. Ein Ring-Drop gilt nur noch als
  Backpressure, wenn die Tiefe wirklich an der Refuse-Schwelle steht --
  ein fabrikneuer Knoten erzwingt keine falsche QRT-Episode mehr.
  `msg_id` aller BP-Rahmen aus einem gemeinsamen monotonen Zaehler
  (rolloverfest) statt `millis()`, sonst kollidieren die zwei Rahmen des
  Drop-Pfads im Dedup-Filter der App.
- **BP-08 -- ganz oder gar nicht.** Das App-Echo einer getippten Nachricht
  ging bisher raus, BEVOR der Ring gefragt war -- eine verworfene
  Nachricht sah im Chat wie gesendet aus, und auf einem Gateway lief der
  UDP-Uplink zum Server unabhaengig vom Ring-Ergebnis: die Nachricht
  erreichte den Server, ohne dass je ein RF-Nachbar sie gehoert haette.
  Jetzt schreibt der Ring zuerst; bei einem Drop kehrt die Funktion vor
  Echo, Buchhaltung und Gateway-Uplink zurueck, der Absender bekommt
  `QTA NOT SENT - <text>`.
- **BP-09 -- der getippte Text ueberlebt eine Abweisung.** `sendMessage()`
  liefert `BpSendResult` (0/-1/-2/-3) aus allen vier Signaturen; T-Deck
  und T-Deck Pro leeren das Eingabefeld nur bei Erfolg (der Tab-Wechsel
  bleibt, damit die Quittung sichtbar ist), die Web-API antwortet
  `sendmessage refused`/`dropped`/`invalid` statt pauschal `ok`, und das
  Web-GUI-JavaScript leert die Felder erst nach der Antwort -- es hatte
  denselben Textverlust eine Ebene hoeher noch einmal eingebaut.
- **BP-10 -- unabhaengige Advisor-Runde ueber BP-07/08/09**, drei echte
  Regressionen gefunden und behoben (T-Deck-Tab-Wechsel unterdrueckte die
  Quittung komplett; Ring-Drop wurde pauschal als Backpressure gelesen;
  nack-Marker loggte den unbereinigten Text), fuenf Medium-Fixes
  (u. a. 140-Byte-Puffer vom nRF52-Loop-Stack, `charset_utf8_safe_truncate()`
  statt Handgestricktem, nack latcht die Episode fuer das spaetere QRV).
  Alles im Endstand oben bereits enthalten.
- **TM-50 -- meshlogger erkennt Zombie-TCP.** Im Overnight-Soak toetete
  ein Router-Reboot die Node-Seite der 2323-Session ohne FIN; der Logger
  sass 2,4 h stumm auf recv()-Timeouts und konnte die Debug-Flags am Ende
  nicht restaurieren. Neu: Stille-Watchdog (`--stall-timeout`, Default
  90 s) in den bestehenden Reconnect-Pfad, SO_KEEPALIVE mit
  Plattform-Tunables, idempotentes Flag-Re-Apply nach jedem Reconnect,
  End-Restore mit einem Retry auf frischer Verbindung. Regressionstest
  gegen einen Fake-Konsolen-Server, fails-before verifiziert.
- **Rueckbau zu Upstream-Reverts.** Upstream hat das erweiterte
  Mheard-JSON (PP-Link-Kette, SRC/GW) und das String-`FWDATE` im
  BLE-I-Register wieder entfernt; dieser Baum zieht nach. Der tote
  PP/DIST-Fail-Soft-Block in `updateMheard()` ist raus, die
  FWDATE-Reparaturen sind mit dem Feld selbst gegenstandslos (das
  Register traegt nur noch `FWVER`). Es bleibt nur, was lebenden Code
  repariert: die HEY-Eingangsschranke und das puffer-begrenzte
  BLE-JSON-Framing.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 485/485 in 12 Host-Envs
  (inkl. DJ8MEH-Replay, Grundlast-Muster, Flood-Test 13-in-10).
- Der BP-07..10-Build lief auf allen vier Bench-Boards: T-Deck Plus
  (DK5EN-14, Quittung sichtbar, Text ueberlebt die Abweisung), Heltec V3
  als Live-Gateway (dk5en-98) und Bench-Node (dk5en-93), T-Beam v1.2
  (dk5en-92) und RAK4631 (dk5en-90).
- Overnight-Soak 2026-09-01 (98/90/93): Recovery sauber, 0 falsche
  BP-Marker im Normalbetrieb, 13 BP-03-Stale-Drops als Feldnachweis des
  Age-Out.

### Was ausdruecklich NICHT geprueft wurde

- Der originale DJ8MEH-Feldvorfall ist weiterhin nicht End-to-End auf
  Hardware nachprovoziert (nur nativ im Replay).
- TM-49 bleibt offen: 4-MB-Boards bei marginalem Link per USB statt OTA
  flashen.
- Batterie-Nullpunkt am echten 2S-Pack, INA226-Zweig und L76K-GPS
  unveraendert ungeprueft.

## Stability-Release v4.35p.08.31.4-stability (2026-08-31, vierter Schnitt)

Vierter Schnitt des Tages; v4.35p.08.31.3-stability bleibt bestehen.
`FLASH_VERSION` bleibt 20260831. Inhalt: die Notice-Politik der
Backpressure, an der Realitaet nachkalibriert (Changelog-Items 161-162),
beide Wellen mit Fable-Advisor-Gate.

### Was dazugekommen ist

- **BP-05 -- QRS ab Tiefe 5, QRV nur nach Refusal.** Eine 5,5-min-Messung
  auf dem Live-Gateway (DK5EN-98) zeigte Grundlast-Ringtiefe 1-4
  (Modalwert 2) und drei falsche QRS/QRV-Paare im voellig normalen
  Betrieb -- die alte QRS-Schwelle (Tiefe > 1) lag AUF der Grundlast,
  jede einzelne Nachricht loeste "slow down" aus. Neu: QRS ab Tiefe 5
  (fix auf allen Boards, defensiv unter der QRT-Schwelle geklemmt), das
  Band 2-4 ist still, und QRV kommt nur noch, wenn die Episode wirklich
  abgewiesen (QRT) oder verworfen (QTA) hat. Das aufgezeichnete
  Grundlast-Muster ist als Regressionstest festgenagelt.
- **BP-06 -- Notices landen im richtigen Chat.** Die Meldung zu einer
  Nachricht in Gruppe 20 kommt als Nachricht an `20` (erscheint im
  20er-Chat), bei einer DM im DM-Thread des Absenders -- nur fuer ihn
  sichtbar, die Rahmen gehen nie on air. Der Refuse-Pfad (lehnt vor dem
  Parsen ab) nutzt einen puren Ziel-Peek mit Paritaet zur echten
  Extraktion (11 Grenzfaelle nativ festgenagelt). Serial und T-Deck-GUI
  bleiben Klartext.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 480/480 in 12 Host-Envs
  (inkl. Feldmessungs-Grundlast als Regressionstest).
- Die Grundlast-Messung selbst stammt vom Live-Gateway DK5EN-98 mit dem
  .3-Build (5,5 min, tools/meshlogger.py ueber die 2323-Konsole).

### Was ausdruecklich NICHT geprueft wurde

- Die .4-Notice-Politik hatte zum Publish noch keine Bench-Zeit auf
  Hardware (Fleet-Flash folgt unmittelbar; erwartet: null [BP]-Marker im
  Normalbetrieb, Nachmessung ausstehend).
- Der originale DJ8MEH-Feldvorfall ist weiterhin nicht auf Hardware
  nachprovoziert; TM-49 bleibt offen (4-MB-Boards: bei marginalem Link
  USB statt OTA).

## Stability-Release v4.35p.08.31.3-stability (2026-08-31, dritter Schnitt)

Ersetzt v4.35p.08.31.2-stability (Release-Objekt geloescht, Tag bleibt als
Marker). `FLASH_VERSION` bleibt 20260831. Inhalt: die drei
Backpressure-RCA-Fixes aus dem DJ8MEH-Feldvorfall (8-Minuten-Refuse-Episode
auf Phantomtiefe) plus eine integrierte Regressions-Suite
(Changelog-Items 157-160). Jeder Fix lief als eigene Welle mit
unabhaengigem Fable-Advisor-Gate; Plan und beide Verdikte sind in
`docs/archive/bp-rca-fixes-*.md` archiviert.

### Was dazugekommen ist

- **BP-02 -- ehrliche Ringtiefe.** `txRingDepth()` und alle Marker
  (`RING_WRITE`/`RING_STATUS`/`TX_GATE_ENTER`/`TX_START`/`RING_TX_READ`)
  zaehlen belegte Slots statt der Indexdistanz `iWrite-iRead`, die
  freigegebene Loecher hinter einem ausgehungerten Eintrag am Lesezeiger
  mitzaehlte (Feldlog: `queued=19` bei real 3-4). `RING_STATUS` traegt die
  alte Distanz zusaetzlich als `dist=`; die RING_ZOMBIE-Detektoren in
  `tools/serial_monitor.py` und `tools/loganalyse.sh` sind portiert
  (queued-Fallback fuer alte Logs).
- **BP-03 -- Stale-HEY-Alterung.** Ein 2-s-Sweep im Main-Loop verwirft
  BACKGROUND-Eintraege (HEY, Prio 5) aelter als 3 min
  (`RING_DROP_STALE`) -- der Feld-Blocker sass 10 min und pinnte die
  Ringfront. Bewusst NICHT in `getNextTxSlot()` (laeuft auf nRF52 im
  Timer-Task); auf nRF52 Check+Drop pro Slot atomar unter dem Ring-Lock,
  EXT_PENDING ausgenommen, Rollover-sicher.
- **BP-04 -- QRV im Wasserband.** Tiefe 0 schliesst sofort wie bisher;
  Tiefe 1 schliesst nach 10 s ununterbrochenem Halt (explizites
  Armed-Flag, Zeit injiziert, Header bleibt Arduino-frei). Der
  Anti-Flap-Fall vom 08.30 ist als Regressionstest festgenagelt.
- **test_bp_regression** (native_aprs): der Vorfall Ende-zu-Ende am echten
  Ring plus echter Statemaschine -- Burst -> QRT -> Drain mit gepinntem
  Blocker -> QRV in 10 s (Feld: 8 min), Blocker-Alterungspfad,
  Gegenprobe QRT-Ausloesung. Jede der drei Fix-Regressionen einzeln per
  Mutation verifiziert.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 459/459 in 12 Host-Envs.
- Die Fleet (DK5EN-14/93/92/90 und Gateway DK5EN-98) laeuft diesen Build
  (98 per Safeboot-OTA mit Build-Stamp-Verifikation, 92 per esptool
  460800, 90 per DFU-Serial).

### Was ausdruecklich NICHT geprueft wurde

- Der originale Feldvorfall (DJ8MEH-Burst auf belastetem Kanal) wurde auf
  Hardware mit dem neuen Build noch nicht nachprovoziert -- der Nachweis
  lebt bisher im integrierten Native-Replay.
- TM-49 bleibt offen (Safeboot-Completion nach Disconnect, Risiko auf
  4-MB-Boards -- bei marginalem Link dort USB statt OTA).
- Die McApp-Darstellung der .2-Notice-Rahmung ist weiterhin nicht am
  Geraet bestaetigt.

## Stability-Release v4.35p.08.31.2-stability (2026-08-31, zweiter Schnitt)

Nachzuegler-Release am selben Tag, ersetzt v4.35p.08.31-stability
(Release-Objekt geloescht, Tag bleibt als Marker stehen). `FLASH_VERSION`
bleibt 20260831 -- rein informativ, keine Struktur-Aenderung. Inhalt: die
OTA-Tooling-Welle vom Nachmittag plus die Backpressure-Notice-Umrahmung
(Changelog-Items 153-156).

### Was dazugekommen ist

- **TM-46 -- Safeboot uebersteht einen abgebrochenen OTA-Upload.** Zentrales
  `abortActiveUpdate()` mit onDisconnect-Hook und Session-Generationszaehler;
  dazu die entscheidende Bench-Findung: ein Cross-Task-Race
  (unsigned-Unterlauf) im Stall-Watchdog konnte gesunde Uploads abbrechen
  (Fix: vorzeichenbehaftete Deltas + volatile). Abort-Retry zweimal auf
  DK5EN-14 bewiesen (Kill 5 s nach Upload-Start -> sofortiger Retry laeuft
  durch, kein `--force`).
- **TM-48 -- Safeboot-WLAN-Join nach Produktionsmuster** (Treiber-gewaehlter
  AP, PMF-off wie TM-34); `[SAFEBOOT];wifi;pmf_off;rc;0` belegt, ~170 kB/s am
  -74-dBm-Schreibtisch-Link.
- **TM-47 -- webflash.py:** Hardware-Strings behalten das `+` (`TDECK+`),
  Safeboot-Resume-Pfad, `--self-test`.
- **BP-01-Nachschliff -- Notices kommen als normale Nachricht vom eigenen
  Rufzeichen an.** BLE/Web: Absender ist das Node-Call statt "response"
  (landete in McApps Spam-Klasse 9999). EXTUDP: statt eigenem Typ "notice"
  jetzt exakt die Form einer ueber LoRa empfangenen Textnachricht (src_type
  "lora", type "msg", numerische Firmware-Version) -- der DJ8MEH-Vorfall vom
  Nachmittag (drei stumm verworfene Nachrichten, RCA aus dem 2323-Log) hatte
  gezeigt, dass keine Client-App die alte Form darstellt. Beide Rahmungen in
  testbare Header extrahiert (`bp_notice_frame.h`, `extern_notice_json.h`)
  und mit zwei neuen nativen Suiten festgenagelt (fails-before gegen beide
  Altverhalten verifiziert).

Die neuen Safeboot-Binaries (`safeboot.bin`, `safeboot-s3.bin`) liegen dem
Release bei; sie kommen nur per USB-Vollflash auf die Boards -- OTA tauscht
ausschliesslich das App-Image.

### Was fuer dieses Release auf Hardware geprueft wurde

- TM-46/47/48: Abort-Retry zweimal auf DK5EN-14 (T-Deck Plus), WLAN-Join-
  Marker und Durchsatz am Schreibtisch-Link; webflash-Runde ueber die Fleet
  (DK5EN-98 per OTA mit Build-Stamp-Verifikation).
- BP-01-Rahmung: nativ festgenagelt (445 Testfaelle, 12 Host-Envs); die
  Fleet (14/93/92/90/98) laeuft den Build mit dem Fix.

### Was ausdruecklich NICHT geprueft wurde

- Die McApp-Darstellung der neuen Notice-Rahmung nach dem Flash (der Test
  vom Nachmittag lief gegen die alte Firmware); Bestaetigung am Geraet steht
  aus.
- TM-49 bleibt offen: der ElegantOTA-Completion-Handler kann nach einem
  Disconnect `hasError==false` sehen und nach einem Teil-Write die
  Boot-Partition umschalten -- auf 16-MB-Boards durch Slot-Validierung
  harmlos, auf 4-MB-Single-Slot-Boards riskant. Guard + Bench-Beweis vor dem
  naechsten Release; bis dahin auf 4-MB-Boards bei marginalem Link USB statt
  OTA.
- Der Backpressure-Phantomtiefen-Befund aus der DJ8MEH-RCA (txRingDepth
  zaehlt freigegebene Loecher hinter einem ausgehungerten Prio-5-Eintrag,
  QRT-Episode dadurch minutenlang zu lang) ist analysiert, aber noch nicht
  gefixt.

## Stability-Release v4.35p.08.31-stability (2026-08-31)

Die komplette Feldkampagne seit dem 08.28-Hotfix: 46 nummerierte Aenderungen
(Changelog-Items 107-152), erarbeitet in zwei /orchestrate-waves-Durchgaengen
auf der Vier-Board-Bench (Heltec V3, T-Beam v1.2, T-Deck Plus, RAK4631 mit
W5100S-Ethernet), abgesichert mit 438 nativen Testfaellen in 12 Host-Envs und
Soak-Laeufen bis 9,1 h. Die vollstaendige Begruendung jeder Aenderung mit
Dateiverweisen und Messwerten steht im PR-Entwurf fuer upstream
(`docs/pr-draft-20260831.md`); das englische Changelog
(`docs/CHANGELOG-stability.md`) traegt die nummerierte Liste.

### Die Schwerpunkte

- **Sicherheit:** Stored XSS in der Web-Nachrichtenseite (jeder LoRa-Absender
  konnte Script im Browser des Operators ausfuehren), drei Web-JSON-Endpunkte
  ohne Escaping, EXTUDP-Serialisierungs-Bounds, 13 BLE-Register-Builder auf
  einen fail-soften Rahmenpfad umgestellt, Guards gegen unkonfigurierte
  `XX0XXX`-Knoten (RX und TX), acht Parser-Befunde, Settings-Plausibilisierung
  nach dem Flash-Laden.
- **Zeit und Uhr:** NTP asynchron (kein 1-s-Blocker mehr) und -- der eigentliche
  Befund -- die NTP-Antwort wurde auf Nicht-Gateways nie gelesen: ein Knoten
  ohne Gateway und GPS bekam NIE eine gueltige Uhrzeit (9,1-h-Soak: 0 von 545
  Antworten geerntet). Dazu `--ntpsync` und MHeard-Alterung ueber die monotone
  Uhr statt der Wanduhr (der ewige NCNT-0-Fehler).
- **WLAN:** Erstverbindung an WPA2/WPA3-Uebergangs-APs (24/24 Fehlschlaege ->
  24/24 Erfolge), ereignisgetriebenes got_ip, asynchrones DNS, gestufter
  Watchdog.
- **Gateway:** Ein ESP32-Gateway relayte UDP-Positionsrahmen nie zu LoRa
  (Selbst-Dedup, 0/30 -> 30/30, beantwortet upstream #568). Und GW-01: das
  Gateway laedt seine eigene HEY nicht mehr selbst zum Server hoch -- die
  nackte Eigenkopie traf per Draht immer Sekunden vor den angereicherten
  Nachbar-Kopien derselben msg_id ein und konnte dort gewinnen; genau darum
  fehlten die Nachbardaten eines Gateways am Server, waehrend `--gateway off`
  sie zeigte. Gemessen am Interlink-Strom des Servers, vorher und nachher.
- **Rueckstau:** Eine in einen vollen TX-Ring getippte Nachricht verschwindet
  nicht mehr wortlos -- Q-Code-Meldungen (QRS/QRT/QTA/QRV) auf dem
  Herkunfts-Transport, nie ueber die Luft.
- **T-Deck:** Audio blockiert die Hauptschleife nicht mehr (~1,1 s pro
  Nachricht), die Karte pannt (i/j/k/l/o, g/h), Trackball per Interrupt,
  GT911-Retry, Boot 14,9 -> 10,9 s. Der Verlust des ersten TFT-Flushes nach
  SD-Zugriff ist instrumentiert und das geclobberte Register benannt:
  ausschliesslich `GPSPI2.clock` (via `--spitrace`); die NOP-Mitigation bleibt
  vorerst drin.
- **Bedienung:** `--maxhop`, Config-Export/-Import ueber die Web-API,
  `--wifi`/`--mute`/Persist-Kommandos seriell, `--display` steuert das
  T-Deck-Panel, `--help` mit 145 statt 95 Kommandos.

### Feld-Debugging ist Absicht

Dieses Release liefert die volle serielle Instrumentierung mit
(`INSTRUMENT_ENABLED=1`, `MC_INJECT_HOOKS=1`): kompakte `[TAG];key;value`-
Marker, zuschaltbar per `--debug`, `--udplog`, `--loradebug`, `--wifistat`,
`--instr` usw., abgreifbar ueber USB oder die Netz-Konsole auf TCP 2323
(`tools/meshlogger.py` fuer Langzeitmitschnitte). Dazu Injektions-Kommandos
(`--injectmsg`, `--injectpos`, `--injectraw` durch den echten RX-Pfad,
`--loratx`-Bursts, am T-Deck Tasten-/Trackball-/Touch-Injektion), damit sich
Feldbefunde ohne zweite Station reproduzieren lassen. Ein Knoten im Feld kann
damit Debug-Logs erzeugen, die wir anschliessend offline auswerten -- genau so
sind alle Befunde dieses Releases entstanden. Einzige Ausnahme: die RAM-
knappste Variante `E22_XML-DevKitC` (klassischer ESP32 plus SoftSerial/XML,
schon bisher ohne Netz-Konsole) baut Mitschnitt-Ring und Injektions-Hooks
nicht mit ein (`MC_CAPTURE=0`, `MC_INJECT_HOOKS=0`) -- die Kampagne hatte
diesen einen Link 648 Byte ueber das DRAM-Segment geschoben. Alle Marker und
Log-Schalter bleiben auch dort verfuegbar.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen sauber; 438 native Testfaelle in 12 Envs gruen.
- Heltec V3: WLAN-/NTP-/Batterie-/OLED-Pfade auf der Bench; GW-01-Fix-Build
  gegen den Live-Serverstrom verifiziert (nur noch angereicherte Kopien).
- T-Beam v1.2: 9,1-h-WLAN-Soak (55/55 Reconnects, 0 unerklaerte Disconnects),
  Beobachter-Gateway der GW-01-Messung.
- T-Deck Plus: komplette Harness-Regression (Boot, Display-CRC, Karte, Nav,
  Input, Heap, Trim, Touch-Injektion) PASS auf diesem Build; --injectraw und
  --loratx live nachgewiesen; `[SPITRACE];clobber`-Messung.
- RAK4631: ETH-01/CTY-01/NTP-Pfade auf der Bench, Bench-Gateway der Kampagne.

### Was ausdruecklich NICHT geprueft wurde

- T-Beam Supreme (L76K-GPS-Zweig ohne Test -- Bench-Module sind u-blox), alle
  E22-/LoRaAPRS-/Vision-Master-/T3-/T-Connect-/T-Echo-/T114-/Wireless-Paper-
  Varianten: bauen sauber, keine Bench-Zeit.
- Batterie-Nullpunkt an einem echten 2S-Pack, INA226-Zweig.
- CONF-Koordinaten werden geparst und geloggt, nicht angewandt.
- T5-E-Paper baut weiterhin nicht (vorbestehender Include-Pfad-Fehler,
  unabhaengig von diesen Aenderungen).

---

## Stability-Release v4.35p.08.28-stability (2026-08-28)

Ein Hotfix-Release. Drei Defekte im BLE-Rahmenpfad zum Telefon, einer davon
aktiv im Feld, dazu die Regressionstests.

### Das I-Register kam nicht mehr an

Upstream-Commit `82db3d41` hat den Wert von `FWDATE` im BLE-Register `I` von
der Ganzzahl `FLASH_VERSION` auf den String `__DATE__ " " __TIME__` umgestellt.
Das sind 14 Zeichen mehr, und damit ueberschreitet das Dokument die Grenze, die
die Firmware sich selbst setzt.

Die Grenze ist leicht zu uebersehen, weil die Zahl, gegen die der Registerbauer
prueft, nicht die ist, die greift: `command_functions.cpp` vergleicht gegen
`MAX_MSG_LEN_PHONE - 2` (298), waehrend die wirksame Klemmung eine Ebene tiefer
in `addBLEComToOutBuffer()` bei 245 Byte sitzt -- abzueglich der Typkennung also
244 Zeichen JSON. Darueber schneidet die Firmware mitten im Wert ab. Das
Ergebnis ist kein verkuerztes, sondern ein syntaktisch kaputtes Objekt: die App
verliert nicht das letzte Feld, sondern alle, also auch `CALL`, `ID` und `HWID`
und damit die gesamte Knotenidentitaet.

Auf dem eigenen Gateway gemessen: **59 verworfene Register in 9,5 Stunden**,
eines je Abgleichzyklus, ab dem Moment des Firmware-Starts. Nach dem Fix: null.

`FWDATE` traegt wieder `FLASH_VERSION`. Der Schluesselname bleibt, nur der Typ
wechselt von String auf Zahl -- und `FLASH_VERSION` ist der Wert, der pro
Release gepflegt wird, waehrend `__DATE__`/`__TIME__` die Uhrzeit des jeweiligen
Compilerlaufs ist.

**Nicht vollstaendig geloest:** ein Knoten mit allen sechs belegten
Gruppenruf-Slots liegt weiterhin 13 Zeichen darueber und war das auch schon vor
`82db3d41`. Die dauerhafte Loesung waere `GCB0..GCB5` als Array (spart 34-41
Zeichen), das ist aber eine fuer Apps sichtbare Kontraktaenderung und gehoert
abgestimmt statt still gemacht.

### Mheard-Datensaetze konnten spurlos verschwinden

Die `PP`-Linkkette im Mheard-JSON waechst je Relais um rund 11 Zeichen. Beim
Standard-Hopwert liegt der Datensatz bei ~214 Zeichen und faellt nicht auf.
`{SET}` laesst bis 7 Hops zu, und dort kippt der Pfad ohne jede Spur:
`addBLEOutBuffer()` klemmt `'D'`-Rahmen erst bei 255 statt bei 245, und die
Schreiblaenge wird in einem `uint8_t` gerechnet. Ab 253 Zeichen JSON wird aus
`blelen + 2` auf ESP32 eine 0 bzw. 1 -- der Rahmen geht als Null- oder
Ein-Byte-Schreibvorgang raus und ist weg, ohne Logzeile. nRF52 ist nicht
betroffen, dort wird derselbe Ausdruck nach `int` promoviert.

Der Bauer misst jetzt vor dem Serialisieren und opfert im Zweifel das teuerste
optionale Feld: erst `PP`, dann `DIST` (aus den Koordinaten beider Stationen
nachrechenbar). Ein Datensatz ohne `PP` ist weiter voll brauchbar, ein auf
Byteebene abgeschnittener ist unparsebar. Das Weglassen wird gemeldet.

Zusaetzlich ist die Kette an der Quelle begrenzt: `appendHeySignalReport()`
haengt nichts mehr an, sobald die naechste Gruppe `HEY_PATH_PAYLOAD_MAX`
sprengen wuerde. Der Wert ist so bemessen, dass die laengste REGULAERE Kette ihn
nie beruehrt -- ein von der Luft kommendes, ueberlanges `@`-Paket dagegen schon.

### Ausserdem

- Zwei benannte Konstanten ersetzen verstreute Zahlen: `BLE_JSON_PAYLOAD_MAX`
  (244) und `HEY_PATH_PAYLOAD_MAX`.
- Vier neue Faelle in `test_hey_report`. Zwei sind ohne den Fix rot, zwei sind
  Schutztests, die festhalten, was sich NICHT aendern darf: eine Kette ueber die
  volle Hoptiefe und der Fall genau an der Kante.
- Zwei Issue-Reports unter `docs/`, einer je Defekt, an die jeweiligen Autoren
  adressiert -- mit Rechnung, Code-Referenzen und Loesungsvorschlaegen.

### Was fuer dieses Release auf Hardware geprueft wurde

- **Heltec V3** (`heltec_wifi_lora_32_V3`) -- per WiFi-OTA auf einen Knoten im
  Live-Betrieb geflasht, Einstellungen unveraendert. Die verworfenen
  `I`-Register hoeren mit dem Neustart auf und sind ueber die folgenden
  Abgleichzyklen nicht wiedergekommen.
- **Alle 32 Release-Umgebungen** bauen sauber.
- **Native-Suiten** `native`, `native_aprs`, `native_dedup`, `native_capture`:
  93 Faelle gruen.

### Was ausdruecklich NICHT geprueft wurde

- Die BLE-Fixes sind nur auf Heltec V3 verifiziert. Der `uint8_t`-Ueberlauf
  betraf nRF52 ohnehin nie.
- Der Fall `PL >= 6` wurde nicht auf echter Hardware provoziert -- der Beleg ist
  die Rechnung plus der Fail-soft-Pfad, nicht ein Feldversuch.
- Ein Knoten mit allen sechs belegten Gruppenruf-Slots wurde nicht getestet;
  fuer den ist das `I`-Register weiterhin zu gross.
- Alles uebrige entspricht `v4.35p.08.27.2-stability`, inklusive der dort
  genannten Luecken.

---

## Stability-Release v4.35p.08.27.2-stability (2026-08-27)

Kein Entwicklungs-Release. Dieses Paket verschiebt die Basis des Forks von
`8114d7ae` (18. August) auf **`upstream/dev` vom 27. August (`fc83554e`)** --
also auf den Stand, **nachdem** Upstream unsere Aenderungen uebernommen hat.

### Der eigentliche Punkt

**Der gesamte Code-Anteil dieses Forks liegt jetzt im offiziellen MeshCom.**
Die ICSSW-Maintainer haben am 27. August beide eingereichten Pull Requests
gemergt:

- **PR #1102** -- 82 Stabilitaets- und Speichersicherheits-Aenderungen aus
  fuenf Monaten Feldbetrieb. 64 Dateien, +3.213/-1.113. Uebernommen als Squash
  `0cac4aea` (Merge `d4dee351`).
- **PR #1103** -- Puffergroesse im neuen FWDATE-Feld von Upstream selbst
  (`2d7f56b1`, Merge `fc83554e`).

Damit ist der Zweck dieses Forks erfuellt: die Verbesserungen sind nicht mehr
"unsere", sie stehen im offiziellen Baum und wirken auch ohne dieses Paket.

### Was aus dem Upstream dazukommt

Alles, was das ICSSW-Team und weitere Beitragende zwischen `8114d7ae` und
`fc83554e` ergaenzt haben -- 26 Dateien, +702/-4.021:

- **SD-Karten-Offlinekarten fuer das T-Deck Plus**, mit dynamischen Kartensaetzen,
  Zoom-Bereichserkennung pro Kartensatz, Auto-Zoom-Rueckfall bei fehlenden
  Kacheln, Nachladen an der Kachelgrenze und mehreren Marker-Korrekturen. Die
  fuenf einkompilierten Kartenblobs (Europa, Deutschland, Oesterreich, Wien,
  Wien-Umgebung -- rund 3.850 Zeilen generiertes C) entfallen zugunsten der
  Kacheln auf der Karte.
- **T-Echo BME280 repariert** -- richtige I2C-Pins, bedingte Adressbehandlung
  fuer BMP280 gegen BME280, ungenutzter zweiter I2C-Bus aus der Variante
  entfernt.
- **Mheard-JSON erweitert** -- die Linkkette der HEY-Bake traegt jetzt RSSI/SNR
  pro Hop, dazu Ursprungsrufzeichen und Gateway-Kennung. `serializeJson`
  bekommt die Puffergroesse statt einer gemessenen Laenge.
- **Info-JSON traegt das Build-Datum** (`FLASH_VERSION`).
- **Neues BLE-TYPE-`I`-Feld `FWDATE`**, damit Apps Sub-Releases unterscheiden
  koennen. `FWVER` bleibt unveraendert.

### Was von uns dazukommt

- **`FWDATE` wurde abgeschnitten.** Upstreams neues Feld legte `char cfwdate[20]`
  fuer `__DATE__ " " __TIME__` an -- noetig sind 21 Byte. `snprintf` kuerzte
  still die letzte Sekundenstelle. Gefunden vom `-Wformat-truncation`-Gate,
  behoben und als PR #1103 zurueckgegeben.
- **Bauhygiene:** ein Sondierungs-Flag im Batteriecode stand ausserhalb des
  `#if defined(ADC_CTRL_PIN)`-Blocks, der es benutzt -- auf Boards ohne diesen
  Pin eine ungenutzte Variable.

### Was sich auf dem Draht aendert

Gegenueber `v4.35p.08.27-stability` nichts. Die drei dort angekuendigten
Aenderungen (`/B=000`, unplausible ACK-Frames werden nicht weitergesendet,
korrigierte ESP32-Kanalauslastung) sind unveraendert und liegen jetzt auch im
offiziellen Upstream.

### Was fuer dieses Release auf Hardware geprueft wurde

Nichts Neues. Dies ist ein Rebase- und Verpackungs-Release; die Bench-Ergebnisse
hinter den eigenen Aenderungen sind die von `v4.35p.08.27-stability` und gelten
unveraendert weiter (OTA Heltec V3 mit erhaltener Konfiguration, OTA T-Beam v1.2,
DFU RAK4631 mit erhaltenem Rufzeichen und Ethernet-Konfiguration).

Geprueft wurde hier:

- Alle 32 Release-Umgebungen bauen sauber.
- Die sieben Haupt-Targets bauen mit `-Werror` auf `src/` ohne eine einzige
  Warnung aus eigenem Code.
- Der Baum ist gegenueber `v4.35p.08.27-stability` in allem ausser den oben
  gelisteten Upstream-Ergaenzungen unveraendert -- per Tree-Hash belegt, nicht
  per Durchsicht.

### Was ausdruecklich NICHT geprueft wurde

- **Upstreams neuer T-Deck- und T-Echo-Code hatte hier keine Bankzeit.** Weder
  ein T-Deck Plus mit SD-Karte noch ein T-Echo mit BME280 steht zur Verfuegung.
  Der Code baut sauber und wird mitgeliefert, aber niemand auf dieser Seite hat
  ihn laufen sehen.
- Alle Punkte aus `v4.35p.08.27-stability` bleiben offen: Batterie-Nullpunkt am
  echten Akku, INA226-Zweig, `--txcapture`-Sendeseite gegen einen zweiten
  Empfaenger, L76K-GPS-Module, Boot am Akku ohne USB-Host.

---

## Upstream-Sync 2026-08-27 (dev)

Upstream hat beide eingereichten PRs uebernommen. `upstream/dev` steht auf
`fc83554e`.

- **PR #1102** -- `Stabilitaets- und Speichersicherheits-Fixes aus dem
Feldbetrieb`: 82 Firmware-Aenderungen, 64 Dateien, +3.213/-1.113. Upstream
  hat sie als Squash `0cac4aea` gemergt (Merge-Commit `d4dee351`).
- **PR #1103** -- `fix(ble): FWDATE-Puffer zu klein`: `char cfwdate[20]` fasst
  `__DATE__ " " __TIME__` nicht (21 Byte noetig), `snprintf` schnitt die letzte
  Sekundenstelle ab. Ein Byte-Fix an Upstreams eigenem Code, gefunden von
  unserem `-Wformat-truncation`-Gate (Commit `2d7f56b1`, Merge `fc83554e`).

Damit liegt der gesamte Firmware-Anteil dieses Forks in `dev`.

**Integriert per Merge, nicht per Rebase.** `dev` traegt unsere 82 Commits als
einen einzigen Squash. Beim Replay der Einzel-Commits darauf bleiben nur
Residuen uebrig: Commits, deren Message einen Fix beschreibt, deren Diff aber
nur noch Loeschungen enthaelt -- verifiziert an
`fix: stack overflow in sendExtern()`, das zu 8 Loeschungen geschrumpft waere.
`git log -p` und `git blame` haetten ab dann falsche Auskunft gegeben. Der
Merge (`ee26403d`) erhaelt die Historie; der resultierende Baum ist
bit-identisch zum Stand davor (Tree `bc849071`), `dev` bringt nichts Neues mit.

Konflikte in 12 Dateien, alle zugunsten dieses Branches aufgeloest -- es sind
genau die Stellen, an denen der PR bewusst vom Fork abweicht:

| Datei                        | Abweichung                                          |
| ---------------------------- | --------------------------------------------------- |
| `platformio.ini`             | `[env:native*]`, `-Werror`, Safeboot-Framework-Hook |
| 10 Dateien unter `src/`      | Kommentare mit Verweisen auf `docs/` und `test/`    |
| `src/txring_functions.cpp`   | zusaetzlich der `NATIVE_BUILD`-Block                |
| `src/configuration_global.h` | `FLASH_VERSION 20260827` statt Upstream-Datum       |

Unsere uebernommenen Commits: 82 (der gesamte Firmware-Anteil).
Verifiziert: alle 7 Targets bauen mit `-Werror` sauber.

---

## Stability-Release v4.35p.08.27-stability (2026-08-27)

Sechs behobene Defekte, ein neues Diagnosewerkzeug und eine Testschicht, die
echte Feldmitschnitte durch den ausgelieferten Code nachfaehrt statt durch eine
Nachbildung.

`FLASH_VERSION` steht jetzt auf `20260827`. Das ist ab diesem Release
gefahrlos: die Ruecksetzbedingung haengt seit dem ersten Punkt unten an
`FLASH_STRUCT_VERSION` (unveraendert `20260724`), nicht mehr am Build-Datum.
Aktualisierende Knoten behalten Rufzeichen, WLAN-Zugangsdaten und
Sensorkonfiguration; auf beiden MCU-Familien nachgemessen.

### Was sich auf dem Draht aendert

Fast alles in diesem Release ist von aussen unsichtbar. Drei Punkte sind es
nicht, und sie stehen hier, damit sie niemanden ueberraschen:

- **`/B=000` wird jetzt gesendet**, wenn die Batterie gemessen und leer ist.
  Bisher ging unterhalb von einem Prozent gar nichts raus -- "leer" und "keine
  Batterie bestueckt" waren nicht unterscheidbar. Ein fehlendes `/B=`-Tag heisst
  ab jetzt: dieser Knoten hat keine Batteriehardware zu melden.
- **Unplausible Frames im ACK-Pfad werden nicht mehr weitergesendet.** Gegen
  8741 Feldframes gemessen sind das 5,7 % dessen, was diesen Pfad erreicht --
  und kein einziger davon quittierte eine Nachricht, die der Knoten tatsaechlich
  gehoert hatte. Bisher wurden sie mit Prioritaet 1 ins Mesh zurueckgesendet.
- **Die ESP32-Kanalauslastung faellt deutlich**, weil sie aus einer festen
  Laenge von 255 Byte gerechnet wurde. Am Funk hat sich nichts geaendert, die
  Zahl stimmt jetzt bloss. Wo derselbe Knoten `util=18%` meldete, sind real rund
  7 % zu erwarten.

### Settings-Persistenz -- jedes Release hat die Konfiguration geloescht (Critical)

Die Ruecksetzbedingung lautete `node_fversion != FLASH_VERSION`, und
`FLASH_VERSION` ist ein Datum, das pro Release hochgezogen wird. Damit hat
JEDES Release die gespeicherte Konfiguration jedes aktualisierenden Knotens
verworfen -- Rufzeichen, WLAN-Zugangsdaten, Sensor- und Netzeinstellungen --,
auch wenn sich am Aufbau von `struct s_meshcom_settings` nichts geaendert hatte.
Nachweisbar am Sprung `20260724` -> `20260821`: der Commit hat weder
`src/esp32/esp32_flash.h` noch `src/nrf52/WisBlock-API.h` angefasst, alle Knoten
wurden trotzdem zurueckgesetzt.

Build-Kennung und Layout-Generation sind jetzt getrennt. `FLASH_VERSION` bleibt
die Release-Kennung und ist rein informativ (`--info`); `FLASH_STRUCT_VERSION`
benennt die Generation des Settings-Layouts und ist der einzige Wert, ueber den
`clear_flash()` entscheidet. Sie steht auf `20260724`, der letzten echten
Layout-Aenderung. Knoten, die nach der alten Semantik `20260821` gespeichert
haben, geniessen Bestandsschutz und werden nicht zurueckgesetzt. Die Logzeile
benennt beides getrennt:

    [INIT]...FLASH layout 20260821 ok, build 20260827

`--flash-reset` setzt weiterhin zurueck.

### Batterie -- ADC_CTRL-Polaritaet wird gemessen statt geraten

Der Umschalter fuer den Spannungsteiler wurde per Compile-Zeit-Test gewaehlt:

    #if defined(BOARD_HELTEC_V31) || defined(BOARD_WIRELESS_PAPER)

`BOARD_HELTEC_V31` ist im gesamten Baum NIRGENDS definiert -- weder in einer
`variants/*/configuration.h` noch in `platformio.ini` noch in `build_flags`. Der
active-LOW-Zweig war damit in jedem je gebauten Image unerreichbar, und jeder
Heltec V3 wurde active-HIGH angesteuert, unabhaengig davon, ob die Platine real
eine 3.0 oder eine 3.1 ist.

Die Polaritaet wird jetzt einmalig beim Boot gemessen. Die Signatur ist
eindeutig und am Geraet nachgemessen: ein durchgeschalteter Teiler liefert
dreistellige Rohwerte, ein getrennter Pin einstellige (enable=HIGH 902-906
Counts, enable=LOW 1-4 Counts, Schwelle 50). Auf einem Board mit bereits
korrekter Polaritaet ist die Aenderung ein No-op -- auf einem Heltec V3 mit
realem Akku bestaetigt. Der Compile-Zeit-Wert bleibt als Startwert und
Rueckfallebene erhalten.

Daraus faellt zum ersten Mal die Unterscheidung "kein Teiler bestueckt" gegen
"Akku leer" ab, auf der der naechste Punkt aufbaut.

### Batterie -- `/B=` auch bei 0 Prozent senden

`mv_to_percent()` klemmt unterhalb `BAT_MIN_VOLTAGE` auf 0, und das `/B=`-Tag
wurde nur `if(global_proz > 0)` geschrieben. Ein fast leerer Akku meldete damit
gar nichts -- der Batteriegraph wird genau dann leer, wenn man ihn braucht. Aus
einer Netzmessung ueber 1230 Stationen: eine reale Station meldet Batterie in
einem Sechs-Stunden-Fenster pro Tag und sonst nie, nur weil der Pack die
3,3-V-Linie kreuzt.

Die beiden `/B=`-Erzeuger in `PositionToAPRS()` waren sich zudem uneinig: der
INA226-Zweig schrieb `/B=%i` voellig ungeprueft, der normale Zweig `/B=%03d` nur
oberhalb 0. Beide schreiben das Tag jetzt, sobald Batteriehardware erkannt ist.
Ein gesendetes `/B=000` heisst "gemessen, und der Akku ist leer"; ein fehlendes
Tag heisst "dieser Knoten hat keine Batterie zu melden". Rueckwaertskompatibel:
der Empfaenger liest das Feld mit `sscanf("%d")`, alle drei Schreibweisen
ergeben denselben Integer.

Mitbehoben: der PMU-Ausfallpfad in `esp32_main.cpp` setzte beide Werte auf 0,
ohne sie als "nicht messbar" zu kennzeichnen. Ohne diesen Zusatz haette ein
T-Beam mit defekter PMU ab sofort dauerhaft `/B=000` gesendet und damit "Akku
leer" behauptet, wo "keine Messung moeglich" gilt.

### ACK-Pfad -- Textbruchstuecke wurden als ACK ins Mesh geflutet (High)

`handleACK()` prueft bisher nur `payload[0] == 0x41` und eine Mindestlaenge.
0x41 ist als ASCII der Buchstabe `A` -- jedes Bruchstueck eines Text- oder
Positionspakets, das damit beginnt, lief dadurch durch die ACK-Verarbeitung:
Bytes 1..4 galten als seine `msg_id`, es landete im Dedup-Ring, wurde mit
Prioritaet 1 in die Sendequeue geschrieben, warf dabei einen Heartbeat aus der
vollen Queue und wurde schliesslich ins Mesh weitergesendet.

Byte 5 ist der belastbare Diskriminator: Funk-ACKs entstehen ausschliesslich als
`(0x80 | max_hop)` und werden auf dem Weiterleitungspfad nur dekrementiert.
Feldmessung ueber 32,7 h und 8741 Frames im ACK-Pfad, drei Knoten:

| Byte 5            | Anzahl        | Byte 10/11 == 01 00 | Feld 6..9 real gehoert |
| ----------------- | ------------- | ------------------- | ---------------------- |
| 0x80..0x84        | 8235 (94,2 %) | 99,6 %              | 58,9 %                 |
| 0x80\|Hops 5..116 | 221 (2,5 %)   | 0,0 %               | 0,0 %                  |
| Bit 7 = 0         | 285 (3,3 %)   | 0,0 %               | 0,0 %                  |

Kein einziger der 506 unplausiblen Frames quittierte eine Nachricht, die der
Knoten tatsaechlich gehoert hatte. Besonders relevant ist die mittlere Gruppe:
eine reine Server-Bit-Pruefung haette sie durchgelassen, und ausgerechnet diese
Frames fuehren Hop-Budgets bis 116. Die Pruefung sitzt als reine Funktion in
`ack_functions.h`, damit sie ohne Hardware testbar ist; ein verworfener Frame
wird als `ACK_REJECT` protokolliert statt still fallengelassen.

### {SET} -- max_hop ohne jede Bereichspruefung

`sscanf` schrieb die Werte direkt in `meshcom_settings`. Ein Tippfehler wie
`{SET}44;2;` landete damit ungeprueft im Hop-Feld jedes ausgesendeten Pakets --
und der Weiterleitungspfad prueft nur `(byte5 & 0x7F) > 0` und dekrementiert,
nach oben war nichts begrenzt. Werte ausserhalb `0..MAX_HOP_LIMIT` lassen den
bisherigen Wert jetzt stehen. Die bisherige Nachsichtigkeit bleibt: jedes Feld
wird einzeln uebernommen, sobald `sscanf` es gelesen hat.

### Neu -- `--txcapture`, Rohframe-Mitschnitt zur Laufzeit

Bisher zeigte das Log Frames nur DEKODIERT, also das Ergebnis unseres eigenen
Parsers. Ein Frame, den der Decoder falsch liest, steht falsch geparst im Log;
nichts darin verraet, was auf dem Kanal lag.

`captureFrame()` legt Rohframes jetzt in einen byteorientierten Ring (768 B),
`captureDrain()` gibt sie aus dem Loop aus. Empfang haengt an `--loradebug`,
Senden am neuen Schalter `--txcapture on/off` (persistiert). Der Ring ist kein
Beiwerk: direkt im Radio-Callback braucht `printfdeb()` rund 900 B Stack -- der
nRF52-Timer-Task hat 1 KB -- und rund 48 ms Serial-Zeit im RX-Pfad. Auf der
TX-Seite saesse er zwischen CAD-Entscheidung "Kanal frei" und `startTransmit()`
und wuerde die Kanalmessung entwerten, auf der der Sendezeitpunkt beruht.
Entkoppelt kostet die Erfassung ein `memcpy`.

Verworfene Frames meldet `[MC-TEST] CAPTURE_DROPPED n= serial_bytes=` -- der
Mitschnitt wird genau dann lueckenhaft, wenn der Kanal voll ist, also in den
Kollisionslagen, um derentwillen man ihn einschaltet. Kosten auf RAK4631:
+1400 B RAM, +1616 B Flash.

### N-29 -- `checkRX()` meldete fuer JEDEN Empfang 255 Byte (High)

Vom neuen Mitschnitt bei seinem allerersten Lauf auf echter Hardware gefunden.

    size_t ibytes = UDP_TX_BUF_SIZE;          // 255
    state = radio.readData(payload, ibytes);

RadioLib nimmt `len` per WERT und schreibt die tatsaechliche Laenge nicht
zurueck; sie ist nur eine obere Schranke. `SX126x.h` sagt das ausdruecklich:
"getPacketLength method must be called BEFORE calling readData!". `ibytes` blieb
also immer 255, und hinter dem echten Frame lag der uninitialisierte
Stackinhalt.

Folge 1, Kanalauslastung um Faktor 1,8-4,1 zu hoch: `checkRX()` bucht
`getTimeOnAir(ibytes)`. Nach jedem Empfang stand exakt `rx=2476ms` im Log -- die
Sendedauer eines 255-Byte-Pakets bei SF11/BW250/CR4:6. Die echten Frames lagen
bei 608 ms (48 B) bis 1394 ms (133 B). Die Gegenprobe steht in derselben Zeile:
`tx=701ms` passt aufs Byte zum 60-Byte-TX-Frame, denn der Sendepfad kennt seine
Laenge. Aus `util=18%` wurden real rund 7 %. Alle ESP32-Auslastungszahlen dieser
Firmware sind zu hoch und waren nie mit nRF52-Zahlen vergleichbar.

Folge 2, Diagnose und Korpus: `CRC_PAYLOAD`, `ERR_PAYLOAD` und der Mitschnitt
hingen rund 190 Byte RAM-Inhalt an jeden Frame.

Die Laenge wird jetzt vor `readData()` mit `radio.getPacketLength()` vom Chip
gelesen, auf `UDP_TX_BUF_SIZE` gedeckelt, und `ibytes == 0` gilt nicht mehr als
Frame.

### N-30 -- `printfdeb()` verdoppelte jedes `%%`

Der `%%`-Zweig schrieb zwei Prozentzeichen und fiel dann in die allgemeine
Kopie, die dasselbe Zeichen erneut anhaengte. Sichtbar als `util=18%%` und
`BATT 100 %%`. Trifft jede Logzeile mit Prozentzeichen und damit auch
`loganalyse.sh` und `logharvest.py`. Die Umbaulogik liegt jetzt in
`src/printfdeb_format.h`, damit sie ohne Arduino nativ pruefbar ist. Mitgefixt:
bei fuehrendem `;` las die alte Schleife vor den Puffer.

### N-31 -- `--info` gab Passwoerter im Klartext an die offene Netzkonsole

`node_passwd`, `node_webpwd` und `node_pwd` wurden unmaskiert gedruckt. Diese
Ausgabe laeuft ueber `printfdeb()` und damit auch auf Port 2323 -- und die
Konsole verlangt ohne gesetztes `node_passwd` keine Authentisierung.
Nachgestellt: `nc <node> 2323`, dann `--info`, und das WLAN-PSK steht auf dem
Schirm. Dieselbe Ausgabe steht in jedem geteilten Logmitschnitt.

`maskSecret()` ersetzt ein gesetztes Passwort durch `***`; leer bleibt leer,
damit ablesbar bleibt OB eins gesetzt ist. Die Settings-JSON an die App bleibt
unberuehrt, sie muss den Wert zum Anzeigen und Aendern tragen. Das ersetzt nicht
`--passwd`, es nimmt dem offenen Port den lohnendsten Fund.

### Tests -- Replay gegen ueber 90 Knotenstunden Feldmitschnitt

`tools/traceharvest.py` erntet die Entscheidungsfolge laufender Knoten aus der
`[MC-DBG]`-Ausgabe und fuettert sie dem echten Code, keiner Nachbildung:

| Suite                | Gegenstand                             | Umfang                             | Abweichungen |
| -------------------- | -------------------------------------- | ---------------------------------- | ------------ |
| `test_dedup_replay`  | `is_new_packet()`, `addLoraRxBuffer()` | 8813 Urteile, 10652 Slotbelegungen | 0            |
| `test_txprio_replay` | `getMessagePriority()`                 | 729 Einstufungen, 5 Klassen        | 0            |
| `test_ack_replay`    | `isPlausibleAckFrame()`                | 30 im Feld honorierte ACKs         | 0            |

Der ACK-Filter ist juenger als diese Logs, es gibt also kein geloggtes Urteil
zum Abgleich. Stattdessen wird eine WIRKUNG genutzt: auf jeden dieser Frames
folgte ein `ACK_RECEIVED`, das einen wartenden Ringslot geschlossen hat. Damit
ist die Frage beantwortbar, die ein nachtraeglich eingezogener Filter immer
aufwirft -- schneidet er ins Fleisch? Er tut es nicht. Alle drei Suiten sind
mutationsgeprueft.

Dazu `test_aprs_reencode` als Interop-Orakel: `encodeAPRS()` reproduziert die
Bytesumme von 2650 verschiedenen real gehoerten Frames AUSNAHMSLOS, ueber
neunzehn gemeldete Hardware-IDs und zwei Firmware-Generationen. Kein
Zirkelschluss -- die Pruefsumme stammt vom Absender, der Decoder verwirft den
Frame, wenn sie nicht zu den Wire-Bytes passt. `test_aprs_fuzz` schickt 500 real
beschaedigte Frames unter ASan und UBSan durch den Parser, mit
Fuell-Differential gegen Lesen ueber das Frame-Ende hinaus.

### Dedup-Ring bleibt bei 100 -- gemessen, nicht geschaetzt

Ein Auswertungsbericht empfahl 100 -> 300. Gegenprobe an denselben 48
Knotenstunden: 112 Rueckkehrer, davon genau **ein** echtes Duplikat, 96
`msg_id`-Wiederverwendungen (eine ANDERE Nachricht mit gleicher ID, Haeufung bei
180-210 min Abstand) und 15 nicht aufloesbare Faelle.

| Ring        | zusaetzlich abgefangen | faelschlich verworfen | Bilanz |
| ----------- | ---------------------- | --------------------- | ------ |
| 150/200/300 | 1                      | 1                     | +-0    |
| 500         | 1                      | 52                    | -51    |
| 1000        | 1                      | 96                    | -95    |

Die Diagnose des Berichts stimmt: das Fenster liegt bei rund 38 min gegen eine
laengste beobachtete Paketlebensdauer von 36,7 min, also ohne Reserve. Nur
faellt durch diese Luecke praktisch nichts. 1 kB RAM fuer ein Ereignis in 48
Knotenstunden ist kein Geschaeft. Die Messung steht als Begruendung im Code, und
`test_dedup_replay` laesst eine Aenderung der Zahl absichtlich auffliegen.

### Werkzeuge

- `tools/meshlogger.py` schneidet die Netzkonsole eines Knotens ueber Tage auf
  Platte mit. Die Konsole bedient einen Client zur Zeit, deshalb gibt das Tool
  den Port auf eine `PAUSE`-Datei hin frei.
- `tools/loganalyse.sh` wurde selbst geprueft und korrigiert (TOOL-01 bis
  TOOL-06): CSMA-Backoff wurde als State-Machine-Fehler gezaehlt, die
  Drop-Aufschluesselung landete im falschen Topf, die Hop-Extraktion las das
  falsche Feld, Stoerbytes in der Eingabe brachten es zum Straucheln, und
  Rohlogs mussten von Hand vorbereitet werden. Es liest sie jetzt direkt, und
  eine Regressionssuite deckt die drei Zaehlfehler ab. Ein Urteil aus einem
  kaputten Messgeraet ist nichts wert -- mehrere Aussagen dieses Release ruhen
  auf diesem Werkzeug.

### Was fuer dieses Release auf Hardware geprueft wurde

Die Nachweise stammen jeweils aus dem Lauf zu der Aenderung, die sie belegen --
nicht aus einem einzelnen Abschlusslauf des fertigen Baums. Fuer den
Release-Stand selbst stehen der Vollbau aller Environments und die native
Testsuite.

- **Heltec V3** -- OTA-Update von `20260821`, Settings unveraendert
  (`FLASH layout 20260821 ok, build 20260827`), zweite OTA im eingeschwungenen
  Zustand ebenfalls ohne Loeschung. ADC_CTRL-Probe `high=971 low=0 -> active
HIGH`, BATT 4.14 V / 90 %, Roh-ADC-Streuung +/-1, Positions-Beacon ausgeloest
  und gesendet (TX_START/TX_DONE), kein `task_wdt`-Ereignis. Rohframe-Mitschnitt
  ueber die Netzkonsole auf Port 2323, fuenf Minuten -- der Lauf, der N-29
  aufgedeckt hat.
- **T-Beam v1.2** (ESP32-D0WDQ6, AXP2101, SX1276) -- OTA-Update, Rufzeichen und
  WLAN-Zugangsdaten erhalten. AXP2101-Batteriepfad unveraendert, BATT 4.15 V /
  100 %, kein `task_wdt`. Der PMU-Ausfallzweig greift dort erwartungsgemaess
  nicht.
- **WisBlock RAK4631** (nRF52) -- DFU-Update, Rufzeichen und
  Ethernet-Konfiguration erhalten, Flash-Version danach `20260724`.

Dazu 6 native Environments mit 220 Testfaellen und der Vollbau aller
Release-Environments.

### Was ausdruecklich NICHT geprueft wurde

- **Der Batterie-Nullpunkt am realen Akku.** `/B=000` ist aus der Logik und aus
  Tests belegt, aber kein Pack auf der Bank war leer genug, um die Meldung im
  Feld auszuloesen.
- **Der INA226-Zweig** von `PositionToAPRS()`. Auf der Bank stand kein Board mit
  INA226; die Aenderung dort ist eine Formatangleichung von `%i` auf `%03d`.
- **Die `--txcapture`-Sendeseite ueber ein echtes Radio.** Der Empfangspfad hat
  auf dem Heltec V3 Frames geliefert, der TX-Mitschnitt ist im selben Lauf mit
  drei Frames aufgetreten, aber nicht systematisch gegen einen Zweitempfaenger
  gehalten.
- **Boards ausserhalb der drei genannten.** Sie bauen aus derselben Quelle und
  erben jede Verbesserung, standen aber nicht auf der Bank -- inklusive T-Beam
  Supreme und aller nRF52-Boards ausser dem RAK4631.
- **Batteriestart ohne USB-Host**, unveraendert seit dem letzten Release nicht
  durchfuehrbar.

---

## Stability-Release v4.35p.08.22-stability (2026-08-22)

> **Nachtrag:** Das Binary dieses Release vergleicht noch das Build-Datum und
> loescht daher beim naechsten Datumssprung die Einstellungen. Die Trennung von
> `FLASH_VERSION` (Build-Kennung) und `FLASH_STRUCT_VERSION` (Layout-Generation)
> ist mit `v4.35p.08.27-stability` veroeffentlicht; ein Update von hier dorthin
> behaelt die Konfiguration.

Korrektur-Release auf `v4.35p.08.21-stability`. **Wer 08.21 installiert hat,
sollte aktualisieren**: dieser Stand hatte einen Defekt, der jeden Node mit
aktiviertem GPS in einen dauerhaften Boot-Loop schickte. Das Release 08.21
wurde deshalb geloescht.

`FLASH_VERSION` bleibt bewusst auf `20260821`. Ein Bump wuerde in
`esp32_main.cpp:732` die gespeicherten Einstellungen jedes aktualisierenden
Knotens loeschen -- Rufzeichen, WLAN-Zugangsdaten, Sensorkonfiguration. Die
Settings-Struktur hat sich nicht geaendert, also gibt es keinen Grund dafuer.

### N-25 -- GPS-Baudscan loest den Task-Watchdog aus (Critical)

Unser eigener Regress, eingefuehrt mit `4c21cb49` (Audit-Befund C3): der
Task-Watchdog wurde in der ersten Zeile von `esp32setup()` abonniert.
`WZ_GPS_Init()` laeuft aber aus `esp32loop()` und blockiert dort rund 16 s ohne
eine einzige Fuetterung -- acht Baudraten zu je 1500 ms, dazu `GPSprobe()` mit
unbegrenztem `readUBX()`-Schwanz. Bei einem Timeout von 5 s bricht der Knoten
zwei bis drei Baudstufen nach Scan-Beginn ab. Da `--gps on` vor dem Absturz
persistiert wird, wiederholt sich das bei jedem Boot: kein Kommandofenster,
keine Rettung ueber Funk, BLE oder Netzkonsole -- nur Reflash ueber USB.

Vier Aenderungen:

- **S1** -- die Watchdog-Subskription wandert ans Ende von `esp32setup()`.
  Setup fuehrt legitim sekundenlange Einmal-Initialisierung aus; sie zu
  ueberwachen war nie Sinn von C3 und hat elf diagnostizierbare Haenger in
  anonyme Boot-Loops verwandelt.
- **S4** -- der GPS-Init-Pfad fuettert den Watchdog, hinter dem einzigen
  Helfer `src/watchdog_feed.h`, der ausserhalb von ESP32 zu nichts kompiliert.
- **S5** -- der Scan bricht bei der ersten NMEA-Sequenz mit gueltiger
  Pruefsumme ab, statt immer alle acht Baudraten zu durchlaufen und danach per
  Argmax die mit den meisten Zeichen zu waehlen. Der alte Argmax hatte keine
  Mindestanzahl (Befund B-15) und konnte aus einem einzelnen Rauschbyte eine
  Phantom-Baudrate erfinden.
- Die Baudratentabelle ist nach Trefferwahrscheinlichkeit sortiert. `SetupUBLOX()`
  stellt Module beim ersten Init dauerhaft auf 38400; mit 9600 vorn lief das
  9600-Fenster danach bei jedem Boot voll aus.

Gemessen, Heltec V3 mit u-blox: **Erkennung von 12 000 ms auf 321 ms**, auf
einem T-Beam bis herunter auf 24 ms.

Zusaetzlich die Aufraeumwelle im selben File: die tote ISR-Variante der
Baudratenerkennung ist entfallen (A-1 bis A-4), inklusive zweier `return -1`
aus einer `unsigned long`-Funktion und unseres eigenen
`pulseIndex + 1`-Regresses. Dabei kam heraus, dass `gps_functions.cpp:24` die
Variantenoption `GPS_BAUDRATE_SOFTCHECK` aus 15 `configuration.h` unbedingt
ueberschrieben hat -- der zweite Zweig war auf ALLEN Boards unerreichbar. Die
Flash-Groesse ist vorher und nachher byteidentisch, was den toten Code belegt.

Volle Analyse: [`docs/bug-N25-gps-baud-scan-watchdog.md`](docs/bug-N25-gps-baud-scan-watchdog.md).
Messprotokoll der Bankpruefung: [`docs/gps-sensor-bench-20260822.md`](docs/gps-sensor-bench-20260822.md).

### N-27 -- BME680-Treiber haelt ein I2C-ACK fuer eine Chip-Erkennung (Medium)

`setupBME680()` setzte `bme680_found` aus dem blossen Adress-ACK und verwarf
den Rueckgabewert von `bme.begin()` -- dabei ist genau das die Stelle, die die
Chip-ID liest. 0x76 und 0x77 teilen sich BME280, BMP280 und BME680. Auf einem
Board mit BME280 meldete der Node `BME680: on (found)` und druckte in jedem
Lesezyklus `Failed to complete reading :(`.

### N-28 -- `--bmx off` konnte den BME680 nicht abschalten (Low, UX)

Die Hilfe sagt seit jeher `--bmx BME/BMP/680 off`, der Handler loeschte aber
nur BMP280/BME280/BMP390. Wer der Hilfe folgte und danach `--bme on` gab, bekam
`BME680 and BMx280 can't be used together!` und stand ohne Sensor da.

### Build -- Upload-Port festgenagelt

Die 27 `upload_command`-Zeilen in `variants/*/platformio.ini` trugen kein
`--port`, `esptool` suchte sich den Port also selbst. Mit zwei angeschlossenen
Boards flasht `pio run --target upload` dann potenziell das falsche. `--upload-port`
war wirkungslos, weil PlatformIO es nur ueber `$UPLOAD_PORT` weiterreicht.

### Was fuer dieses Release auf Hardware geprueft wurde

- **Heltec V3** mit u-blox-GPS und BME280 -- Boot-Loop auf dem alten Stand
  reproduziert und hier als behoben bestaetigt; GPS-Neuinitialisierung per
  Kommando und per Dreifachklick; 36 Minuten Dauerlauf mit GPS und Sensor;
  Sensortyp korrekt bestimmt.
- **Heltec V3, zweites Geraet, ohne GPS-Modul** -- Scan laeuft zu Ende, meldet
  ehrlich einen Fehlschlag und erfindet keine Baudrate.
- **T-Beam v1.2** (ESP32-D0WDQ6, AXP2101, SX1276, u-blox) -- neu in Betrieb
  genommen. Boot mit WLAN-Verbindung und GPS-Init im selben Durchlauf, also
  genau die Sequenz, an der es vorher scheiterte.
- **WisBlock RAK4631** (nRF52) -- geflasht und betrieben. Auf nRF52 ist
  ueberhaupt kein Task-Watchdog scharf, der Defekt trifft diesen Pfad nicht.

### Was ausdruecklich NICHT geprueft wurde

- **T-Beam Supreme.** `ttgo_tbeam_supreme.bin` ist wieder im Release -- der
  Grund fuer das Zurueckhalten war dieser GPS-Defekt, und der ist behoben. Auf
  einem Supreme verifiziert wurde es aber nicht. Konkrete Luecke: der Supreme
  traegt ein **L76K**, beide Module auf der Bank waren u-blox. Der
  L76K-Zweig in `GPSprobe()` ist von keinem Test beruehrt.
- **Batteriestart ohne USB-Host.** Auf dieser Bank nicht durchfuehrbar: dem
  Heltec V3 ist `ARDUINO_USB_CDC_ON_BOOT=1` auskommentiert, der klassische
  T-Beam hat kein natives USB.
- **Der Phantom-Baudraten-Fall mit verrauschtem Eingang.** Belegt ist, dass
  keine Phantom-Baudrate gemeldet wird; der freie Eingang las null Zeichen, der
  eigentliche Ausloeser liess sich also nicht erzeugen.

---

## Stability-Release v4.35p.08.21-stability (2026-08-21)

> **Zurueckgezogen.** Dieses Release hatte den oben beschriebenen Defekt N-25
> und wurde geloescht. Nicht mehr installieren.

Qualitaets-Release auf Basis der offiziellen MeshCom 4.35p (upstream `dev`,
Merge-Base `8114d7ae`). **Kein Feature-Release und kein On-Air-Change** -- ein Node
mit diesem Build verhaelt sich gegenueber Mesh, Nachrichten und Apps wie das
offizielle 4.35p. Geaendert wurden Stabilitaet, Robustheit, Eingabe-Haertung und
die Testinfrastruktur; dazu einige kleine Diagnose- und Wartungshilfen
(`--dfu` fuer den UF2-Bootloader, Reset-Ursache beim Boot, Entwickler-Tools).

Schwerpunkte:

- **Eingabe-Haertung** auf allen drei Empfangspfaden -- LoRa/APRS, BLE und
  UDP/Netz. Format-String-Auswertung empfangener Texte im Debug-Logger,
  Laengenpruefungen der BLE-Konfigurations- und Textnachrichten, URL-Decode,
  UDP-Off-by-One, APRS-Trailer/FCS.
- **Crash- und Freeze-Fixes**, gefunden auf echter Hardware: Stack-Overflow im
  Loop-Task, Watchdog-Trip beim Boot mit Gateway-Konfiguration, eingefrorener
  Loop bei EXTUDP/Webserver ohne initialisiertes Ethernet, W5100S-Warteschleifen.
- **Nebenlaeufigkeit**: TX-Ring-Enqueue vollstaendig unter einem Lock,
  Snapshot-Lesen des UDP-Rings, BLE-Callback auf dem nRF52 entkoppelt.
- **Zeitrobustheit**: `millis()`-Wraparound-sichere Vergleiche an 25 Stellen.
- **Testinfrastruktur**: native Host-Suiten (`native`, `native_aprs`,
  `native_extradio`), Test-Orakel mit Frame-Korpus, Mock-MeshCom-Server,
  Ressourcen-Waechter mit RAM/Flash-Baseline, CI-Build-Gate ueber alle
  `default_envs`.

Die vollstaendige Auflistung mit Referenz auf die jeweiligen Findings steht in
[`docs/CHANGELOG-stability.md`](docs/CHANGELOG-stability.md). Die Befunde selbst
sind in [`docs/architecture/08-defect-catalogue.md`](docs/architecture/08-defect-catalogue.md)
und [`docs/code-audit-fixes-20260627.md`](docs/code-audit-fixes-20260627.md)
dokumentiert; offene Punkte in [`docs/BACKLOG.md`](docs/BACKLOG.md).

---

## Upstream-Sync 2026-08-18 (dev)

Rebase auf aktuellen upstream/dev (HEAD 8114d7ae). 29 neue Commits aus upstream
seit 2026-07-12:

- Externe Radio-Anbindung ueber TCP (PR #1072, makrohard) -- der mit Abstand
  groesste Block (19 Commits, ~2.480 Zeilen Produktivcode plus ~2.160 Zeilen
  Host-Tests). Neue Libraries
  `lib/external_radio_protocol`, `lib/external_radio_tcp`,
  `lib/external_radio_txq`, ESP32-Glue in `src/esp32/external_radio_glue.cpp/.h`,
  Protokoll-Doku `docs/external-radio-protocol.md`. Umfasst Frame-Protokoll und
  Stream-Parser, TCP-Transport, asynchrone TX-Queue mit Slot-Ownership,
  RX-Callback, Channel-Busy-/Retransmit-Behandlung und Config-Sync nach dem
  Country-Setup. Beruehrt auch `lora_functions.cpp/.h`, `lora_setchip.cpp/.h`,
  `esp32_main.cpp`, `nrf52_main.cpp`, `loop_functions.cpp`,
  `command_functions.cpp`. Der Firmware-Build ist opt-in
  (`[env:esp32-external-radio]`, nicht in `default_envs`), die Host-Tests laufen
  in der neuen `[env:native_extradio]`.
- v4.35p Softser Watchdog-Timer -- HEY-Intervall wird bei `ENABLE_SOFTSER` um
  10 Minuten verlaengert (`esp32_main.cpp`).
- v4.35p neues Kommando `--setlog on/off` inkl. Korrektur der Kommando-Maske
  (`command_functions.cpp`).
- v4.35p neues Laendersetting PL (Polen).
- v4.35p RAK/WisBlock GPS TRACK MODE und GPS SAT-INFO (`nrf52_main.cpp`).
- v4.35p `E22_1262-DevKitC` mit `ENABLE_GPS`; HELTEC_V3 `ADC_FACTOR` angepasst.
- Update `batt_functions.cpp` und `loop_functions.cpp` (Minor-Fixes).

Konflikt beim Rebase in `src/esp32/esp32_main.cpp`: Upstream hat im Trickle-HEY
Zweig ein `extra_hey_time` (10 Minuten Zuschlag bei `ENABLE_SOFTSER`) auf die
alte, nicht wraparound-sichere Bedingung `(heyinfo_timer + trickle_interval_ms +
extra_hey_time) < millis()` addiert. Aufgeloest durch Uebernahme beider
Aenderungen: der Zuschlag bleibt, die Pruefung bleibt unsere wraparound-sichere
Form `(uint32_t)(millis() - heyinfo_timer) >= (trickle_interval_ms +
extra_hey_time)`.

Nachzug nach dem Rebase in `platformio.ini`: Upstream hat die lose Datei
`test/compress_functions.*` nach `test/test_compress/` verschoben und drei
weitere Suiten unter `test/test_external_radio_*` angelegt. Damit zog unsere
`[env:native]` diese Suiten mit ein und `pio test -e native` (unser CI-Gate)
brach beim Linken ab. Behoben mit `test_filter = test_regex_call` -- upstreams
Suiten laufen in `env:native_extradio`, unsere Env bleibt auf unsere eigenen
Host-Tests beschraenkt.

Unsere uebernommenen Commits: keine. Alle 35 lokalen Commits sauber appliziert
(1 Konflikt manuell aufgeloest, siehe oben).

---

## Upstream-Sync 2026-07-12 (dev)

Rebase auf aktuellen upstream/dev (HEAD 2832e192). Grosser Sync, 63 neue
Commits aus upstream seit 2026-06-26:

- TBEAM 1W Deepsleep deaktiviert; Wireless Paper Deepsleep-Pfad an Vision
  Master E213 angeglichen (Stego-Lab, PR #1050).
- E218 GPS aktiviert; NCount-Fix "from Hey only >= 4.35p" (mehrere Commits).
- Neue Hardware-Variante ESP32-LoRaPRS (E22/RA01) -- platformio.ini,
  Variant-Configs, mehrere Folge-Commits (dl1mx, PR #1029).
- Terminalkommando --rotate 0/90/180/270 (E-Ink Display drehen), Fix
  Buffer-Overflow-Grenze im {MCP}-Handler (sizeof(clfd) statt sizeof(cset))
  (Stego-Lab, PRs #1033/#1034).
- Security-Fixes: Buffer-Overflow in TinyGsmClientSequansMonarch.h (snprintf
  statt sprintf, PR #1019, orbisai0security); V-001 Security-Vulnerability-Fix.
- Heltec E213: 180 Grad gedreht, diverse v4.35p-Minor-Fixes; GPS-T114 und
  BTCODE Fixes.

Konflikt beim Rebase in `loop_functions.cpp`: Upstream hat einen neuen
`intervall == 0xFFFF` Zweig (nur LoRa-APRS) direkt vor unserer
millis()-wraparound-sicheren Bedingung eingefuegt. Aufgeloest durch Uebernahme
beider Aenderungen (neuer 0xFFFF-Zweig aus upstream + unsere wraparound-sichere
Pruefung).

Unsere uebernommenen Commits: keine. Alle 26 lokalen Commits sauber appliziert
(1 Konflikt manuell aufgeloest, siehe oben).

---

## Upstream-Sync 2026-06-26 (dev)

Rebase auf aktuellen upstream/dev (HEAD d3af8986). Neue Aenderungen aus upstream:

- Heltec E213: neues Board (ESP32-S3 + SX1262 + 2.13" E-Ink, PR #1021, Stego-Lab)
- v4.35p minor (PR #1020)
- v4.35p default settings (PR #1018)
- v4.35p heltec v3 yota (PR #1017)
- v4.35p init heltec v3 (PR #1016)
- v4.35p T-DECK SETUP (PR #1014)
- v4.35p via -- mehrere Commits (PRs #1013, #1010, #1009)

Unsere uebernommenen Commits: keine. Alle 17 lokalen Commits sauber appliziert,
kein Konflikt.

---

## Upstream-Sync 2026-06-11 (dev)

Rebase auf aktuellen upstream/dev (HEAD 871da1ad). Grosser Sync mit umfangreichen
Source-Aenderungen (59 Dateien, +4077/-1629). Neue Aenderungen aus upstream:

- via/routing-Ueberarbeitung -- mehrere Commits "v4.35p via/routing" und
  "routing, optical" (`via_functions.cpp/.h`, `loop_functions.cpp`,
  `lora_functions.cpp`, `udp_functions.cpp`).
- Neues Debug-Modul printfdeb -- `printfdeb_functions.cpp/.h` (printlndeb,
  printdeb, printfdeb), CSV/Manual de/en.
- Netconsole ohne USB/WiFi -- Panic/Reboot verhindern, wenn Netconsole ohne
  WiFi-Verbindung laeuft (`net_console.cpp`); Nutzung ohne Gateway/Webserver.
- batt_function -- Batterie ohne Akku (0V -> 100%), Korrektur ESP32-E22-PCB BATT,
  Wireless-Paper BATT-Messung (`esp32_pmu.cpp`, `batt_functions.cpp`).
- web_functions -- Fix doppelte HTML-Element-ID im Web-Interface (#998),
  web_functions Updates (`web_functions.cpp`, `web_setup.cpp`).
- Heltec Wireless Paper Variante (E0213A367, HW-ID 57) -- neue Variante auf
  dev-Basis, 24 Commits (E-Ink-Tuning, Boot-Screen, GPS/Track-Seiten,
  Batterie-Skalierung, TCXO via DIO3); betrifft uns nur ueber gemeinsame Dateien.
- Weiteres: S3/T3 1.3 Support, Download-Tool, README HW-Update, safeboot.bin.

Unsere uebernommenen Commits: keine. Alle 16 lokalen Commits sauber appliziert.
Einziger Konflikt: `FLASH_VERSION` in `configuration_global.h` -- auf upstream-Wert
20260608 aufgeloest (wird ohnehin erst beim Build gesetzt). Kein
`.load()/.store()`-Mismatch. Unser einziger Source-Delta bleibt
`extudp_functions.cpp` (sendExtern-Fix, +10) und `platformio.ini`.

## Upstream-Sync 2026-05-31 (dev)

Rebase auf aktuellen upstream/dev (HEAD eba328b4). Neue Aenderungen aus upstream:

- v4.35p web_function (4a0fe3e2) -- Labels "APRS Symbol" und "APRS Group" in der
  Web-Setup-Seite (`web_functions.cpp`, `sub_page_setup`) vertauscht; reine
  UI-Korrektur, keine Logik-Aenderung.

Unsere uebernommenen Commits: keine. Alle 14 lokalen Commits wurden sauber
appliziert, keine Konflikte, kein `.load()/.store()`-Mismatch.

Hinweis: Die Merge-Commits #957-#960 (WebService fix, netconsole stop,
no log_functions, RAK SSID weg) waren bereits ueber den 05-25 Rebase auf
v4.35p.05.23 in unserem Branch enthalten; einzig `web_function` ist neu.

Build-Test vor dem Push: `heltec_wifi_lora_32_V3` (ESP32) und `wiscore_rak4631`
(nRF52) erfolgreich kompiliert -- beide Zweige der plattformbedingten
`sendExtern`-Pufferallokation abgedeckt.

---

## Upstream-Sync 2026-05-17 (dev)

Wechsel des Upstream-Targets von `oe1kbc_v4.35p` auf `dev`. Hintergrund: Upstream hat
nahezu alle Versions-Branches geloescht und entwickelt jetzt aktiv auf `dev`, in das
sowohl `oe1kbc_v4.35p` als auch der neue `oe1kbc_tls` Branch gemerged wurden.
`oe1kbc_v4.35p` ist nun in `dev` enthalten und liegt 19 Commits zurueck.

Rebase auf aktuellen upstream/dev (HEAD 9c9e1908). Neue Aenderungen aus upstream:

- v4.35p tlsconsole + iram reduction (67311a4c)
- Lazy ssl_context allocation -- Heap-Reduktion auf Low-RAM Nodes (e6ce9f20, von DH1FR)
- v4.35p tls disable (431cbb1f)
- DISABLE_TLS_CONSOLE Guard fuer Low-RAM Boards (E22_XML-DevKitC) (0972ec73, von DH1FR)
- v4.35p tls_console excl. on xml (e637fa65)
- Fix: guard tlsConsoleSetPassword calls with #ifdef ESP32 (nRF52 build) (d2ff12b7)
- Fix: use auto& for s_hwSerial to support USBCDC on ESP32-S3/S2/C3 (309796d0)
- Rename Telnet -> TLS-Console: tls_console.cpp/.h, --tlsconsole cmd, --passwd none/status
- Fix --telnet command: status query, prevent match against --tel telemetry handler
- v4.35p code review (01d55b71)
- v4.35p checkmesh (2083fdbd)

Unsere uebernommenen Commits: keine.

Konflikt-Aufloesung: Trivialer Whitespace-Konflikt in `platformio.ini` (Zeilenaufteilung
der NimBLE-Flags `-DCONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4` / `-Wall -Wextra`).
Saubere zweizeilige Form uebernommen, Inhalt identisch.

---

## Upstream-Sync 2026-05-09 (oe1kbc_v4.35p)

Rebase auf aktuellen upstream (HEAD aa457d8). Neue Aenderungen aus upstream:

- v4.35p.04.08 mh distance (760c368)
- v4.35p.04.08 mh corr (aa457d8)

Unsere uebernommenen Commits: keine.

---

## Upstream-Sync 2026-04-19 (oe1kbc_v4.35p)

Rebase auf aktuellen upstream (HEAD b531a17). Neue Aenderungen aus upstream:

- v4.35p t_echo (f79df08)
- v4.35p wireless_tracker (2648c7d)
- v4.35p SX1262 min power -9dBm -- 27 variant configs angepasst (546ad01)
- v4.35p heltec_t114 corr -- Duplikat [nrf52] Section entfernt (fd1cbd3)
- v4.35p.04.19 -- E22-DevKitC/E22_1262/ttgo-lora32-v21 lib_ignore erweitert, GPS am E22_1262 deaktiviert (41a2d88)
- v4.35p.04.19 -- nrf52 onebutton + loop_functions Anpassungen (bbcff2f)
- v4.35p.04.19 -- nrf52_functions Korrekturen (c114322)
- v4.35p.04.19 -- command_functions Korrekturen (b531a17)

Unsere uebernommenen Commits: 1 -- unser T114/RAK4631-Fix (00b166e) wurde von upstream in fd1cbd3 adaptiert und beim Rebase automatisch weggelassen.

### Bekanntes Problem: E22-DevKitC DRAM Overflow

Der Build fuer `E22-DevKitC` (AZ-Delivery DevKit V4, SX1268) schlaegt auf diesem Upstream-Stand mit 40 Byte DRAM-Overflow fehl:

```
ld: region `dram0_0_seg' overflowed by 40 bytes
```

Reproduzierbar auf reinem upstream/oe1kbc_v4.35p ohne unsere Commits -- **Upstream-Defekt**, nicht durch unser Patchset verursacht. Ursache sind die zusaetzlichen statischen Allokationen aus 546ad01 (SX1262 Power-Range) und 41a2d88 (E22 lib_ignore/GPS Aenderungen), die den klassischen ESP32 (nicht-S3) DRAM ueberschreiten.

Betroffen ist nur `E22-DevKitC`. Alle anderen 6 Standardtargets (Heltec V3, T-Beam, T-Beam Supreme, T-Deck, T-Deck Plus, WisBlock RAK4631) bauen sauber.

---

## Upstream-Sync 2026-04-17 (oe1kbc_v4.35p)

Rebase auf aktuellen upstream (HEAD 95bd4c4). Neue Aenderungen aus upstream:

- download_meshcom.py Update
- GPS SoftSerial Anpassungen
- release.md Update
- T-Echo: SPI auf SPIM3 umgestellt
- T-Echo Aenderungen
- T-Beam 1W RX Switch Fix
- nRF52 RX Boost
- Merge master -> oe1kbc_v4.35p
- T-Beam max_txpower auf 17 dBm fixiert
- E290 GPS Pins geaendert

Unsere uebernommenen Commits: keine (4 lokale Commits unveraendert oben).

---

## Basis: MeshCom Firmware v4.35p (2026-03-23)

Diese Version baut auf v4.35p auf.

---

## Log-Analyse Tool (tools/loganalyse.sh)

Umfassende Offline-Analyse von MeshCom Serial-Monitor Logs. Erzeugt strukturierte Abschnitte (`=== SECTION ===`) fuer maschinelle und menschliche Auswertung.

### Analyse-Sektionen

**Basis-Analyse:**
Overview, Active Nodes, Message Types, Hop Distribution, Loops, Channel Utilization, ACK Analysis, CRC Errors, Ring Status, Missing ACKs, Dedup, State Machine, Additional, CRC Detail, CAD Attempt Distribution, CAD Storm, Ring Overflow, Dropped Packets, High Hop Packets, Priority Distribution, Trickle HEY, High Water Marks

**Erweiterte Analyse (neu):**

- **HEAP Monitoring** -- Trend, Stabilitaetsbewertung (Leak-Erkennung), Low/High Watermark
- **Starvation Events** -- Radio Silent (>20s), Stuck States, Ring Zombies, Prio-1 Starvation
- **Server Connection** -- OeVSV Heartbeat-Gaps, WiFi Events, UDP Resets
- **NTP Sync** -- Sync-Intervall, Health Rating
- **Signal Profiling** -- RSSI/SNR/Freq-Error pro direkt gehoertem Node (H00) mit Standardabweichung
- **Frequency Drift** -- Tag/Nacht-Vergleich, Temperatur-Drift-Erkennung, lineare Regression
- **Interferer Detection** -- Pakete mit >5kHz Frequenzabweichung, off-frequency Nodes
- **Top Talkers** -- Meistgehoerte Nodes, TX/Relay-Aufschluesselung, geschaetzte Airtime
- **Channel Util Diagram** -- ASCII-Balkendiagramm (stuendlich)
- **CSMA Timing** -- Adaptive-Wait-Verteilung, Inter-Arrival, CAD-Busy vs Traffic, Bewertung
- **ACK Storm** -- Burst-Erkennung, ACK-Effizienz
- **CAD Enhanced** -- False-Positive-Rate, Giveup-Tracking
- **CRC Forensics** -- Hex-Payload-Dekodierung, Callsign-Extraktion, Kollisions-Klassifikation (Hidden Node vs CAD Failure vs Timing), Top-Kollisionspaare
- **BLE TX Latency** -- End-to-End Latenz BLE-Empfang bis LoRa-TX, Queue-Tiefe-Korrelation, Histogramm

### Nutzung

```bash
./tools/loganalyse.sh <logfile> [<logfile2>]
```

Mit zwei Logfiles: zusaetzliche Cross-Correlation Analyse.

---

## Supported Hardware

Dateinamen wie im GitHub-Release. Massgeblich sind `default_envs` in
`platformio.ini` und die Artefaktliste in `.github/workflows/meshcom-ci.yml`.

### ESP DevKits + E22 LoRa Modul

- E22-DevKitC.bin (433 MHz)
- E22_XML-DevKitC.bin (433 MHz)
- E22_1268_S3-DevKitC-1-N16R8.bin (433 MHz)
- E22_1262-DevKitC.bin (868 MHz)
- E22_1262_S3-DevKitC-1-N16R8.bin (868 MHz)

### ESP32 Lora-Aprs

- esp32-loraprs-e22.bin
- esp32-loraprs-ra01.bin

### HELTEC

- heltec_wifi_lora_32_V2.bin
- heltec_wifi_lora_32_V3.bin
- heltec_wifi_lora_32_V4.bin
- heltec_wireless_stick.bin
- heltec_wireless_tracker.bin
- wireless-paper.bin
- vision-master-e290.bin
- vision-master-e213.bin
- heltec_t114.zip, .uf2

### Lilygo

#### TBEAM

- ttgo-lora32-v21.bin
- ttgo_tbeam.bin
- ttgo_tbeam_SX1262.bin
- ttgo_tbeam_SX1268.bin
- ttgo_tbeam_supreme.bin
- T-Beam-1W.bin

#### T-DECK

- t_deck.bin
- t_deck_plus.bin
- t_deck_pro.bin

#### T-Echo

- t_echo.zip, .uf2

#### T3 S3

- T3_S3_V13.bin

#### T_CONNECT_PRO

- t_connect_pro.bin

### RAK Wisblock

- wiscore_rak4631.zip, .uf2

### Safeboot / OTA

- safeboot.bin, safeboot-s3.bin
- bootloader.bin, bootloader-s3.bin
- partitions.bin, otadata.bin

### Gebaut, aber nicht im Release

- `t5_epaper`, `vision-master-e213-preview`, `esp32-external-radio` -- Opt-in,
  nicht in `default_envs`.

`T-ETH-ELITE_1262` steht zwar nicht in der Artefaktliste des Release-Workflows,
wird aber seit 08.21 von Hand mitveroeffentlicht und ist im Release enthalten.

### Newer version > v4.35 able to upgrade via OTA-Flasher.

### [MeshCom Changelog](https://icssw.org/meshcom-versionen/)

### [MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
