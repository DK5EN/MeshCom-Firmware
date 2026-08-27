# Release Notes -- MeshCom Firmware v4.35p

Firmware `4.35p`, `FLASH_VERSION 20260827`, `FLASH_STRUCT_VERSION 20260724`
(`src/configuration_global.h`).
Aeltere Eintraege bis einschliesslich 2026-03-22 stehen im Archiv
[`docs/release_lora_trx.md`](docs/release_lora_trx.md).

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

### Tests -- Replay gegen 48 Knotenstunden Feldmitschnitt

`tools/traceharvest.py` erntet die Entscheidungsfolge laufender Knoten aus der
`[MC-DBG]`-Ausgabe und fuettert sie dem echten Code, keiner Nachbildung:

| Suite                | Gegenstand                             | Umfang                            | Abweichungen |
| -------------------- | -------------------------------------- | --------------------------------- | ------------ |
| `test_dedup_replay`  | `is_new_packet()`, `addLoraRxBuffer()` | 5647 Urteile, 6869 Slotbelegungen | 0            |
| `test_txprio_replay` | `getMessagePriority()`                 | 505 Einstufungen, 5 Klassen       | 0            |
| `test_ack_replay`    | `isPlausibleAckFrame()`                | 30 im Feld honorierte ACKs        | 0            |

Der ACK-Filter ist juenger als diese Logs, es gibt also kein geloggtes Urteil
zum Abgleich. Stattdessen wird eine WIRKUNG genutzt: auf jeden dieser Frames
folgte ein `ACK_RECEIVED`, das einen wartenden Ringslot geschlossen hat. Damit
ist die Frage beantwortbar, die ein nachtraeglich eingezogener Filter immer
aufwirft -- schneidet er ins Fleisch? Er tut es nicht. Alle drei Suiten sind
mutationsgeprueft.

Dazu `test_aprs_reencode` als Interop-Orakel: `encodeAPRS()` reproduziert die
Bytesumme von 2422 verschiedenen real gehoerten Frames AUSNAHMSLOS, ueber elf
Hardware-IDs und zwei Firmware-Generationen. Kein Zirkelschluss -- die
Pruefsumme stammt vom Absender, der Decoder verwirft den Frame, wenn sie nicht
zu den Wire-Bytes passt. `test_aprs_fuzz` schickt 500 real beschaedigte Frames
unter ASan und UBSan durch den Parser, mit Fuell-Differential gegen Lesen ueber
das Frame-Ende hinaus.

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
