# Anfrage DO8AIL: "External Device"-Schnittstelle über UART2 am T-Beam V1.1 -- Verdict

Datum: 2026-09-06
Anfrage von: Tom, DO8AIL (Mail vom 06.09.2026, 09:52)
Bearbeitet von: DK5EN
Zielplattform der Anfrage: LilyGo T-Beam V1.1 (klassischer ESP32, env `ttgo_tbeam`)
Entscheidung: **abgelehnt für den T-Beam V1.1**, Verweis auf das bestehende extUDP-Interface

---

## 1. Die Anfrage

Tom baut einen Tacho für einen Traktor-Oldtimer. Datenquelle soll ein T-Beam mit
Firmware 4.35S sein, Anzeige ein Nextion-Display. Aktuell behilft er sich mit einem
zweiten ESP (D1 Mini) als Zwischenglied.

Sein Vorschlag für eine künftige Firmware-Version:

- Generischer "External Device"-Anschluss, nicht Nextion-spezifisch, sondern als
  allgemeine Schnittstelle für externe Displays und Bediengeräte.
- Beim T-Beam V1.1 über den freien UART2, UART1 bleibt beim GPS.
- Einfaches, dokumentiertes JSON-Protokoll über UART, 115200 Baud.
- Ausgaben bei Änderungen bzw. Events: GPS (Lat/Lon/Höhe), Geschwindigkeit, Kurs,
  Satellitenzahl, HDOP, GPS-Fix, Batterie, ADC-Werte (kalibriert), Telemetriedaten,
  RSSI/SNR, Callsign, Firmwareversion, empfangene Nachrichten.
- Bidirektional: das externe Gerät übergibt `{"type":"msg","dst":"..","msg":".."}`
  und die Firmware nutzt den normalen Sendeweg.
- Vorgeschlagene Beispiele:

  ```json
  {"type":"pos","lat":52.123456,"lon":10.123456,"alt":84,"speed":18.4,"course":247,"sat":9,"hdop":1.2,"fix":1}
  {"type":"msg","src":"DO9XYZ-7","dst":"DO8AIL-14","msg":"Wo bist du?"}
  {"type":"msg","dst":"DO8XXX-12","msg":"Hallo von Tom"}
  ```

Motivation: der T-Beam als eigenständige Bordstation mit Display und Bedienung, oder
als Standalone-Gerät im Shack mit großem 7-Zoll-Display.

## 2. Was die Firmware heute schon hat

Der Vorschlag ist inhaltlich fast deckungsgleich mit dem bestehenden extUDP-Interface
(`src/extudp_functions.cpp`, UDP-Port 1799, Doku in `docs/ext_udp_telemetry.md`):

| Wunsch von Tom                         | Stand in extUDP                                                                           |
| -------------------------------------- | ----------------------------------------------------------------------------------------- |
| `{"type":"pos", ...}` bei Positionen   | vorhanden: `lat`, `lat_dir`, `long`, `long_dir`, `alt`, `batt`, `rssi`, `snr`, `firmware` |
| `{"type":"msg", ...}` bei Empfang      | vorhanden: `src`, `dst`, `msg`, `msg_id`, `rssi`, `snr`, `firmware`                       |
| Nachricht einspeisen (bidirektional)   | vorhanden: `{"type":"msg","dst":"..","msg":".."}` geht über `sendMessage()`               |
| Telemetrie einspeisen (Tankgeber)      | vorhanden: `{"type":"tele","temp":..,"hum":..,"press":..}` landet in der Positionsbake    |
| Callsign, Firmwareversion              | vorhanden: `src`, `firmware`, `fw_sub`                                                    |
| Lebenszeichen                          | vorhanden: Heartbeat                                                                      |
| Geschwindigkeit, Kurs, Sats, HDOP, Fix | **nicht vorhanden** (die Werte existieren in `gpsData`, werden aber nicht exportiert)     |
| kalibrierter ADC-Wert                  | **nicht vorhanden**                                                                       |
| Transport UART                         | **nicht vorhanden**, einziger Sink ist `UdpExtern`                                        |

Achtung bei der Feldbenennung: die Länge heißt `long`, nicht `lon`.

Das Variantenheader `variants/ttgo_tbeam/configuration.h` trägt außerdem den Hinweis
`//#define ENABLE_SOFTSER    //do not enable on TBEAM !!`. Eine SoftwareSerial-Lösung
kommt für den T-Beam nicht in Frage, es müsste ein Hardware-UART sein.

## 3. Was fehlen würde

Technisch wäre die Erweiterung klein:

- Ein zweiter Sink (`HardwareSerial` auf GPIO 13/14 des T-Beam-Headers) neben `bUDP`
  in `sendExtern()` und `sendExternNotice()`, dieselbe JSON-Zeile plus Zeilenende.
- Ein Zeilenleser, der `getExtern()` füttert.
- Ein Setting `--extser on/off`, Pins im Variantenheader.
- Die fehlenden GPS-Felder (`speed_kmh`, `course`, `satellites`, `hdop` aus `gpsData`)
  in den `pos`-Frame für `src_type == "node"`.

Geschätzt 150 bis 250 Zeilen, kein neues Protokoll, kein neuer Parser.

## 4. RAM-Lage am T-Beam V1.1: gemessen

Sauberer Build von `fork-main` (Commit `1ce83e0f`), env `ttgo_tbeam`, 2026-09-06,
`xtensa-esp32-elf-size -A`:

| Region                          | belegt    | Limit     | frei              |
| ------------------------------- | --------- | --------- | ----------------- |
| iram0_0 (Vektoren + IRAM-Text)  | 131.051 B | 131.072 B | **21 B (0,02 %)** |
| dram0_0 statisch (.data + .bss) | 114.600 B | 124.580 B | 9.980 B (8 %)     |
| Flash                           | 1,63 MB   | 3,40 MB   | 52 %              |

Zum Vergleich der Stand aus `docs/archive/ram-comparison-20260517.md`: damals lag dram0_0 bei
98,95 % (1.304 B frei) und iram0_0 bei 99,98 % (28 B frei). Die Ringpuffer-Verkleinerung
hat die statische DRAM-Lage von "gar nichts" auf "unter 20 %" gebracht; das ist die
Mindestreserve, kein Spielraum. Der IRAM ist unverändert am Anschlag.

Was das für die Anfrage bedeutet:

- **IRAM ist die harte Grenze.** 21 Byte Reserve heißt: jede Funktion, die absichtlich
  oder versehentlich mit `IRAM_ATTR` in den Instruction-RAM gelinkt wird, jeder
  Toolchain- oder Bibliotheks-Sprung, bricht den Link. Der UART-Treiber samt ISR ist
  zwar für `Serial` bereits gelinkt, so dass ein zweiter `HardwareSerial`-Instanz
  nominell kein IRAM kostet. Aber die Marge ist null, und ein Feature auf einem Target
  mit null Marge zu bauen heißt, es beim nächsten unabhängigen Commit wieder zu
  verlieren.
- **Statischer DRAM ist ebenfalls voll.** Die 9.980 B (8 %) sind keine verfügbare
  Reserve, sondern die strategische Reserve für den Betrieb: Stack-Spitzen, Treiber-
  und Bibliotheksversionen, Upstream-Merges. Unter 20 % Reserve gilt das Target als
  voll, und wir liegen deutlich darunter. Die Erweiterung würde etwa 300 bis 600 B
  kosten (eine `HardwareSerial`-Instanz, ein Zeilenpuffer von ~256 B) und damit aus
  genau dieser Reserve abgezweigt werden. Das geben wir nicht her.
- **Heap zur Laufzeit:** rund 256 B für den UART-RX-Ring plus das transiente
  ArduinoJson-Dokument, das extUDP ohnehin anlegt. Der freie Heap auf dem T-Beam V1.2
  der Bench (DK5EN-92) wurde in dieser Sitzung **nicht** gemessen, kein Bench-Node war
  am USB. Erfahrungswert klassischer ESP32 mit WiFi und BLE: 40 bis 70 kB frei.
- **Flash:** 5 bis 10 kB, irrelevant.

Der KISS-TNC-Vorschlag (PR 1114, Review in `docs/pr1114-kiss-review-20260901.md`) ist
die zweite Anfrage in kurzer Zeit, die auf dem klassischen ESP32 an derselben Wand
steht. Der T-Beam V1.1 bekommt keine neuen Schnittstellen mehr, solange der IRAM nicht
um einige hundert Byte entlastet ist (bekannter Hebel: die PSRAM-Flags im Board-JSON,
siehe Memory "Classic ESP32 IRAM/DRAM levers").

## 5. Was es später brechen könnte

- **Loop-Task-Stack.** `sendExtern()` ist der tiefste extUDP-Pfad (PT-01). Ein
  `Serial2.write` von dort ist flach und unkritisch, aber JSON-Aufbau im seriellen
  Empfangspfad darf nur im Main-Loop laufen, nie aus `OnRxDone` oder einem Timer.
- **Blockierende Writes.** `HardwareSerial::write` blockiert bei vollem TX-FIFO, wenn
  der Peer langsam liest. 500 B bei 115200 Baud sind etwa 45 ms. Ein hängendes Nextion
  darf den Loop nicht anhalten, also TX-Puffergröße oder Drop-on-full ins Design.
- **Protokoll-Einfrieren.** Sobald eine Display-Firmware im Feld auf die Feldnamen
  baut, sind extUDP und ein extSER gemeinsam eingefroren. Die GPS-Felder gehörten dann
  in einem Schritt hinein, nicht nachträglich.
- **Upstream.** Ein neues Setting, ein Pin-Paar, ein Flag in einer bestehenden
  Funktion passt zur Minimal-Change-Regel, aber Kurt müsste einen neuen UART-Nutzer
  auf dem T-Beam-Header akzeptieren.

Korrigierte Annahme aus der Diskussion: eine serielle Ausgabe mit 115200 Baud
blockiert die CPU nicht und macht LoRa-RX nicht taub. Der UART hat FIFO und Interrupts,
der LoRa-Empfang läuft im Radio-Interrupt. Das Argument gegen die Erweiterung ist der
IRAM am Limit und der Pflegeaufwand, nicht die Baudrate.

## 6. Empfohlener Weg für Tom: extUDP -> D1 Mini -> Nextion

- T-Beam als Access Point (`--wifiap on`), `--extudp on`, `--extudpip <IP des D1>`.
- D1 Mini hängt im WLAN des T-Beam, empfängt UDP 1799, reicht die JSON-Zeilen seriell
  an das Nextion weiter und schickt Eingaben als `{"type":"msg",...}` zurück.
- Tankgeber: `{"type":"tele",...}` vom D1 an den T-Beam, der Wert landet in der Bake.
- Geschwindigkeit, Kurs, Sats, HDOP: die TX-Leitung des GPS-Moduls (UART1) zusätzlich
  auf den D1 legen, der die NMEA-Sätze (RMC, GGA) direkt mitliest. Das GPS sendet nur,
  der T-Beam merkt nichts davon.

Damit bekommt Tom heute das gleiche Protokoll, das er sich für UART gewünscht hat. Sein
Display-Code müsste sich nicht ändern, falls später doch ein serieller Sink kommt.

## 7. Versendete Antwort (Wortlaut, 2026-09-06)

```text
Hallo Tom,

erst mal danke für dein ausführliches Mail. Ich habe es mir angeschaut, die Idee ist gut,
und sie ist auch nicht neu für uns: fast genau das, was du beschreibst, gibt es in der
Firmware bereits, nur nicht über UART, sondern über WLAN/UDP. Das Interface heißt extUDP.

Warum UART2 nicht funktioniert:

Der T-Beam V1.1 mit dem klassischen ESP32 ist bereits heute am Anschlag. Das Firmware-Image
belegt den Instruction-RAM bis auf 21 Byte, das Daten-RAM ebenso voll. Jede
Erweiterung auf dieser Plattform ist ein Risiko für alle anderen Funktionen. Eine zweite serielle Schnittstelle
wäre technisch klein, aber sie zieht Tests, Doku und ein Protokoll nach sich, das wir
dann auf Dauer stabil halten müssen.

Aktuell gibt es schon die Anfrage mit dem KISS TNC. Auch das Thema ist uns um die Ohren geflogen,
weil das RAM ausgeht.

Was du heute schon nutzen kannst, und zwar mit deinem D1 Mini:

Der T-Beam schickt über extUDP JSON-Zeilen an eine IP-Adresse, Port 1799, und nimmt auf
demselben Weg Nachrichten zum Senden entgegen. Das ist genau die Struktur, die du dir
für UART gewünscht hast, nur eben über WLAN. Dein D1 Mini spielt die Brücke: er hängt im
WLAN des T-Beam, empfängt die UDP-Pakete und reicht sie seriell an das Nextion weiter.
Umgekehrt schickt er, was das Nextion eingibt, als UDP-Paket zurück an den T-Beam.

Einrichtung am T-Beam (per BLE-App oder USB-Konsole):

  --wifiap on              der T-Beam spannt ein eigenes WLAN auf
  --extudp on              extUDP einschalten
  --extudpip 192.168.4.2   die IP deines D1 Mini im T-Beam-WLAN
  --reboot

Alternativ hängst du beide in dein Heim-WLAN (--setssid / --setpwd) und trägst dort die
IP des D1 Mini ein. Auf dem Traktor ist der Access-Point-Modus die einfachere Variante.

Was der T-Beam sendet (jeweils eine JSON-Zeile pro Ereignis):

  Eigene und empfangene Positionen:
  {"src_type":"node","type":"pos","src":"DO8AIL-7","lat":52.123456,"lat_dir":"N",
   "long":10.123456,"long_dir":"E","alt":84,"batt":87,"firmware":"4.35","fw_sub":"s",
   "rssi":-97,"snr":8,"hw_id":3,"msg_id":12345,"aprs_symbol":"[","aprs_symbol_group":"/"}

  Empfangene Nachrichten:
  {"src_type":"lora","type":"msg","src":"DO9XYZ-7","dst":"DO8AIL-14",
   "msg":"Wo bist du?","msg_id":12346,"rssi":-101,"snr":5,"firmware":"4.35","fw_sub":"s"}

  Dazu ein regelmäßiger Heartbeat, damit der D1 weiß, dass der T-Beam lebt.

Was der D1 Mini an den T-Beam schicken kann:

  Nachricht senden (geht über den normalen Sendeweg der Firmware):
  {"type":"msg","dst":"DO8XXX-12","msg":"Hallo von Tom"}
  {"type":"msg","dst":"*","msg":"an alle"}

  Sensorwerte einspeisen, die dann in der Positionsbake landen (Tankgeber!):
  {"type":"tele","temp":23.3,"hum":60,"press":1018.5}

GPS: Geschwindigkeit, Kurs, Satellitenzahl und HDOP
sind in der Positionszeile heute nicht enthalten, nur Position, Höhe und Batterie. Für
deinen Tacho ist das der Knackpunkt. Der pragmatische Weg: das GPS-Modul des T-Beam
hängt an UART1, und du kannst dessen TX-Leitung zusätzlich auf den D1 Mini legen. Der
D1 liest dann die NMEA-Sätze direkt mit (Speed, Kurs, Sats, HDOP stehen alle im RMC
und GGA) und nimmt Nachrichten und Batterie vom extUDP. Das GPS sendet nur, der T-Beam
merkt davon nichts.

Die Beschreibung des Interfaces liegt im Repo unter docs/ext_udp_telemetry.md, und der
Quellcode dazu in src/extudp_functions.cpp, falls du selbst nachsehen willst.
https://github.com/DK5EN/MeshCom-Firmware

Wenn du damit weiterkommst, wäre eine kurze Rückmeldung schön, wie stabil der Weg über
WLAN auf dem Traktor läuft. Das ist ein interessantes und wirklich cooles Projekt.

Grüße an dich in die Heide,
73 de Martin, DK5EN aus Freising
```

## 8. Offen

- Freien Heap auf DK5EN-92 (`--info`) nachmessen, sobald der T-Beam wieder an der
  Bench hängt, und hier eintragen.
- BACKLOG-Zeile "IRAM-Marge `ttgo_tbeam` 21 B" als eigenes Thema, unabhängig von
  dieser Anfrage.
- Falls Tom Rückmeldung zur WLAN-Stabilität auf dem Traktor gibt: hier anhängen.
