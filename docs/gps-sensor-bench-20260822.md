# GPS- und Sensor-Bankprüfung, 2026-08-22

**Zweck:** N-25 (GPS-Baudscan löst den Task-Watchdog aus) auf echter Hardware
reproduzieren, die Wellen 0/1/2 des Fixplans verifizieren und die angeschlossene
I2C-Sensorik prüfen.

**Branch:** `v4.35p_prio` · **Ausgangsstand:** `f77e6a28` · **Endstand:** `7bd313bd`
**Bänke:** Heltec V3 (frisch, `/dev/cu.usbserial-0001`), RAK4631 `DK5EN-90`
(`/dev/cu.usbmodem2101`)

---

## 1. Ergebnis in einem Satz

N-25 ist auf einem zweiten Board unabhängig reproduziert und durch die Wellen 0,
1 und 2 behoben; die Erkennungsdauer fällt von 12 000 ms auf 123–362 ms. Der
T-Beam v1.2 ist als dritte Plattform in Betrieb genommen und durchläuft genau
die Sequenz, an der die gemeldete Supreme starb — WiFi-Verbindung, dann
GPS-Init — ohne Abbruch. Auf dem RAK4631 ist kein GPS-Modul erreichbar; das ist
kein Regress unserer Firmware, der Upstream-Release verhält sich identisch.

---

## 2. Heltec V3

### 2.1 Reproduktion von N-25 (Stand `f77e6a28`, unverändert)

`--gpsdebug on`, dann `--gps on`:

```
 37.048  --gps on[GPS ]...Init GPIO RX=47 TX=48
 38.474  [GPS ]...1200 baud --> 4 chars
 39.891  [GPS ]...2400 baud --> 7 chars
 41.509  [GPS ]...4800 baud --> 10 chars
 41.916  E (47658) task_wdt: Task watchdog got triggered ... loopTask (CPU 1)
 42.120  abort() was called at PC 0x4206371c on core 0
 42.121  E (6141) esp_core_dump_flash: Not enough space to save core dump!
 42.121  Rebooting...
```

Abbruch **4,9 s** nach Scan-Beginn, drei Baudstufen tief — deckt sich mit
Abschnitt 11 des Bug-Dokuments ("zwei oder drei Baudstufen"). Danach
Dauer-Boot-Loop: `--gps on` war bereits persistiert.

Der Backtrace ließ sich gegen die exakte ELF (SHA256 `7778525e3221dfae`)
symbolisieren und bestätigt Abschnitt 2 des Bug-Dokuments:

| Frame        | Symbol                                  |
| ------------ | --------------------------------------- |
| `0x40377d9e` | `panic_abort` — `panic.c:408`           |
| `0x4038111d` | `esp_system_abort` — `esp_system.c:137` |
| `0x403870cd` | `abort` — `abort.c:46`                  |
| `0x4206371c` | `task_wdt_isr` — `task_wdt.c:176`       |
| `0x40379db1` | `_xt_lowint1` — `xtensa_vectors.S:1118` |
| `0x420f0783` | `cpu_ll_waiti` / `esp_pm_impl_waiti`    |
| `0x42063ed9` | `esp_vApplicationIdleHook`              |
| `0x4038259c` | `prvIdleTask` — `tasks.c:4099`          |

Der zweite Stack ist der unterbrochene `IDLE0`. Der ausgehungerte Task ist
`loopTask` auf CPU 1, wie der Panic-Kopf meldet.

### 2.2 Welle 0 + 1 (`dae2d863`, `f20b922d`)

```
  4.468  [GPS ]...Init GPIO RX=47 TX=48
  5.888  [GPS ]...1200 baud --> 6 chars
  ...
 13.417  [GPS ]...38400 baud --> 960 chars
 16.466  [GPS ]...found with 38400 baud (960 chars)
 19.720  [GPS ]...UBLOX erkannt
```

Scan läuft vollständig durch, **kein** `task_wdt`-Ereignis. Dauer wie geplant
unverändert: 12,0 s Scan, 15,3 s bis `UBLOX erkannt`. Modul ist ein u-blox auf
38400 Baud.

### 2.3 Welle 2 (`7bd313bd`)

```
 16.265  [GPS ]...Init GPIO RX=47 TX=48
 17.897  [GPS ]...9600 baud --> 14 chars
 18.504  [GPS ]...38400 baud --> 62 chars (NMEA ok)
 18.505  [GPS ]...found with 38400 baud (62 chars, NMEA-Pruefsumme gueltig, 2140 ms)
```

| Messung                    | vorher    | Welle 2     | + Umsortierung (`f190aad3`) |
| -------------------------- | --------- | ----------- | --------------------------- |
| Baudratenerkennung, Boot 1 | 12 000 ms | **2120 ms** | **321 ms**                  |
| Baudratenerkennung, Boot 2 | 12 000 ms | **2140 ms** | **362 ms**                  |
| `--gps reset` im Betrieb   | Absturz   | **2309 ms** | —                           |
| `--gps off` + `--gps on`   | Absturz   | **2249 ms** | —                           |
| `WZ_GPS_Init()` gesamt     | Absturz   | ~4,9 s      | ~2,8 s                      |

Die 14 bzw. 23 Zeichen im 9600-Fenster sind reines Rauschen des auf 38400
sendenden Moduls — genau der Fall, den der alte Argmax nicht von einem Modul
unterscheiden konnte (B-15).

`WZ_GPS_Init()` gesamt liegt bei ~4,9 s, weil nach der Erkennung noch die
u-blox-Konfiguration folgt (`UBX_CFG_PRT`/`RATE`/`CFG`, Flash-Save,
`UBX_MON_VER`). Das ist B-9 und gehört zu Welle 4, nicht zu Welle 2.

### 2.4 Dauerlauf

32 Minuten mit GPS und BME280 gleichzeitig aktiv: **0** `task_wdt`-Ereignisse,
**0** ungeplante Neustarts (die zwei protokollierten Boots sind beide selbst
ausgelöst). GPS durchgehend `fix:yes`, 7–8 Satelliten, HDOP 1,0–2,4.

### 2.5 Sensor

`--showi2c` findet genau ein Gerät: **0x76**. Der Scanner kann dort nicht
unterscheiden — BME280, BMP280 und BME680 teilen sich die Adresse.

Bestimmung durch Messung, nicht durch Annahme:

| Schritt               | Ergebnis                                                        |
| --------------------- | --------------------------------------------------------------- |
| `--bme on`            | `[INIT]...BME280 startet`, T 25,13 °C, rF 40,67 %, p 961,90 hPa |
| Gegenprobe `--680 on` | `Failed to complete reading :(` — kein einziger Messwert        |

Feuchte wird geliefert, also **kein BMP280** (der hat keinen Feuchtesensor; die
Bibliothek schaltet die Feuchte-Überabtastung nur `if (bmx280.isBME280())`
frei). Die Gegenprobe schließt den BME680 aus. Der Druck passt zur Höhe: 961,9
hPa QFE bei den vom GPS gemeldeten ~500 m, QNH 1017 hPa.

**Ergebnis: BME280 an 0x76.**

Dabei sind zwei Defekte aufgefallen, jetzt als **N-27** und **N-28** im
Katalog: der BME680-Treiber hält ein blosses Adress-ACK für eine Chip-Erkennung,
und `--help` verspricht ein `--bmx off`, das den BME680 gar nicht abschalten kann.

### 2.6 Grösse

| Ziel                     | Flash vorher | Flash nachher | Δ        | RAM         |
| ------------------------ | ------------ | ------------- | -------- | ----------- |
| `heltec_wifi_lora_32_V3` | 1 383 529    | 1 383 985     | **+456** | unverändert |
| `ttgo_tbeam_supreme`     | 1 420 173    | 1 420 629     | **+456** | unverändert |
| `wiscore_rak4631`        | 577 952      | 577 968       | **+16**  | unverändert |

Auf nRF52 nur +16 Byte: `meshcom_wdt_feed()` kompiliert dort zu nichts, übrig
bleibt der NMEA-Prüfer abzüglich des entfallenen Argmax.

---

## 3. RAK4631 (`DK5EN-90`)

Kein GPS gefunden:

```
 24.062  GPS: trying 9600 baud
 27.318  GPS: trying 38400 baud
 30.779  GPS: speed not found
 32.608  [GPS ]...wait[GPS ]...Sende UBX_SET_GNSS
 32.608  [GPS ]...Sende UBX_MON_VER
 33.216  [GPS_VER]
```

`--showi2c` meldet `no devices found` — auch kein Sensor, kein RTC, kein
MCP23017, kein INA226, kein Display.

**Kontrollfall.** Upstream-Release `v4.35p.08.20` (`wiscore_rak4631.zip`, über
`adafruit-nrfutil` seriell geflasht) zeigt auf demselben Board Zeile für Zeile
dasselbe Verhalten, inklusive `no devices found`. Das Modul ist also nicht
erreichbar; unsere Firmware ist nicht die Ursache. Danach wieder auf unseren
Stand zurückgeflasht, Konfiguration unverändert (`DK5EN-90`, 192.168.68.68).

Der Pfad ist als **N-26** aufgenommen: die fehlgeschlagene Erkennung wird
nirgends festgehalten, `SetupUBLOX()` läuft bedingungslos danach, und es kostet
rund 9 s Boot-Zeit auf jedem RAK ohne Modul. N-25 trifft diesen Pfad nicht — auf
nRF52 ist überhaupt kein Task-Watchdog scharf.

---

## 4. T-Beam v1.2 (`DK5EN-92`) — neue Plattform

Vorher nie geprüft. Board: ESP32-D0WDQ6-V3, 4 MB Flash, 4 MB PSRAM, AXP2101-PMU,
SH1106-OLED, u-blox-GPS an RX=34/TX=12. Flash vollständig gelöscht
(`esptool erase_flash`), dann frisch bespielt und als `DK5EN-92` konfiguriert
(`-90` und `-98` waren auf der Luft belegt).

### 4.1 Welche Variante

Drei Envs bauen für dieses Board und unterscheiden sich nur im Funkchip. Statt
zu raten: `ttgo_tbeam` (SX127X) geflasht und den Init gelesen —
`[LoRa]...SX1276 Chip Initializing ... success`. Damit ist `ttgo_tbeam` die
richtige Variante, `ttgo_tbeam_SX1262` und `ttgo_tbeam_SX1268` sind ausgeschlossen.

### 4.2 GPS

| Fall                                           | Dauer bis `found with ...`  |
| ---------------------------------------------- | --------------------------- |
| Erstkontakt, Modul ab Werk 9600, 9600 zuerst   | 535 ms                      |
| Reboot danach (Modul jetzt 38400), 9600 zuerst | 1956 ms                     |
| Reboot, 38400 zuerst (`f190aad3`)              | **180 / 123 / 107 / 24 ms** |

Der mittlere Wert ist der Grund für `f190aad3`: `SetupUBLOX()` stellt das Modul
beim ersten Init dauerhaft auf 38400, das 9600-Fenster lief danach bei jedem
Boot vollständig aus, bevor 38400 an die Reihe kam.

Trigger A (Reboot mit persistiertem `--gps on`) auf diesem dritten Board
mehrfach durchlaufen: kein `task_wdt`, kein `abort()`, der Node kommt jedes Mal hoch.

Der Empfänger arbeitet. Zunächst nur Zeit und Datum aus gültigen NMEA-Sätzen
und 4–5 sichtbare Satelliten ohne Positions-Fix — nach rund acht Minuten am
selben Platz dann `fix:yes`, 8 Satelliten, HDOP 1,1. Das war Kaltstartzeit, kein
Firmware-Befund; der Heltec V3 daneben lag zeitgleich bei 9 Satelliten und
HDOP 0,9.

### 4.3 WiFi

Erst nach `--webserver on` baut der Node überhaupt eine Station-Verbindung auf.
`--setssid` und `--setpwd` allein genügen nicht: solange weder Webserver noch
Gateway aktiv sind, wird `startNetwork()` nicht betreten und im Log erscheint
keine einzige `[WIFI]`-Zeile. Wer die Zugangsdaten setzt und auf eine Verbindung
wartet, wartet vergeblich, ohne eine Fehlermeldung zu sehen.

Danach verbindet der Node sauber: Kanal 9, RSSI -55 dBm, IP per DHCP.

### 4.4 Der eigentliche Feldfall

Ein Boot mit WiFi **und** GPS ist genau die Sequenz, an der die gemeldete
Supreme starb — im Feldlog folgt auf `[WIFI]...connecting` der GPS-Init und dann
der Abbruch. Auf dem T-Beam:

```
267.163  CLIENT SETUP
273.463  [WIFI]...SSID: <ssid> CHAN: 9 RSSI: -55
273.463  [WIFI]...connecting to CHAN: 9
274.478  [GPS ]...found with 38400 baud (62 chars, NMEA-Pruefsumme gueltig, 123 ms)
276.909  [GPS ]...UBLOX erkannt
276.909  [WIFI]...connect OK
276.909  [WIFI]...now listening at IP ..., UDP port 1990
```

9,7 s vom Reset bis zum verbundenen Node, kein `task_wdt`. `startNetwork()` ist
der N-17-Pfad; auch der bleibt unter dem scharf geschalteten Watchdog unauffällig.

### 4.5 Sensorik

`--showi2c` findet nur `0x34` (AXP2101-PMU) und `0x3C` (SH1106-OLED). Kein
externer Sensor an diesem Board.

---

## 5. Heltec V3 ohne GPS-Modul (`DK5EN-91`) — §8.4 und §8.3

Zweites Heltec-V3-Board, kein GPS angeschlossen. Das ist der direkte Test des
B-15-Fixes und zugleich der letzte offene Trigger aus §8.3.

### 5.1 Drei Firmware-Stände auf demselben nackten Board

| Stand                                           | Verhalten                                                                                          |
| ----------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| vor dem Fix (Flash-Version 20260724, war drauf) | **Abbruch nach 5,09 s, Boot-Loop**                                                                 |
| Upstream `v4.35p.08.20`                         | Scan läuft 12 s durch, `[GPS_ERR] Erkennung fehlgeschlagen`, kein Absturz (kein Watchdog upstream) |
| unser Stand (`f190aad3`)                        | `keine gueltige NMEA-Sequenz auf 8 Baudraten (12000 ms)`, dann `[GPS_ERR]`. Kein Abbruch.          |

Der Fall **ohne** Modul ist vor dem Fix schlimmer als der mit Modul: der Scan
findet nirgends etwas, läuft also in die volle Länge, und die Debug-Ausgabe je
Baudstufe hängt an `GPS_BAUDS_RX[iGpsBaud] > 0`. Ohne empfangene Zeichen druckt
sie nichts. Der Betreiber sieht `[GPS ]...Init GPIO RX=47 TX=48` und danach
unmittelbar den Watchdog — keine einzige Zeile dazwischen, die sagt, woran es
liegt.

### 5.2 Was dieser Test NICHT zeigt

B-15 beschreibt eine Phantom-Baudrate, die entsteht, wenn ein einzelnes
Rauschbyte im passenden Zeichenbereich auf dem ungezogenen `GPS_RX_PIN` landet.
Genau dieser Auslöser trat hier **nicht** auf: der Pin las auf allen acht
Baudraten null Zeichen. Deshalb meldete auch der alte Argmax korrekt einen
Fehlschlag — auf demselben Board mit Upstream-Firmware nachgeprüft.

Belegt ist damit: der neue Code meldet keine Phantom-Baudrate und stürzt nicht
ab. **Nicht** belegt ist, dass er einen verrauschten Pin übersteht. Für diesen
Nachweis müsste Rauschen eingekoppelt werden — offener Punkt.

Zum Vergleich: auf dem Board **mit** Modul zählte das 9600-Fenster 14 bis 23
"passende" Zeichen, obwohl das Modul auf 38400 sendet. Das ist der Mechanismus
aus B-15, aber dort mit einer Quelle. Ein wirklich freier Eingang war auf dieser
Bank still.

### 5.3 Trigger C — Tastendruck

`onebutton_functions.cpp:210` bildet den Dreifachklick auf `--gps on` plus
`--track on` ab. Ausgeführt auf dem Board ohne Modul, der Callback trieb also
einen vollen Scan:

```
727.581  [GPS ]...Init GPIO RX=47 TX=48
739.601  [GPS ]...keine gueltige NMEA-Sequenz auf 8 Baudraten (12001 ms)
739.602  [GPS_ERR] Erkennung fehlgeschlagen (Timeout oder kein Signal)
```

Kein `task_wdt`, kein `abort()`, kein Neustart — Laufzeit des Knotens
durchgehend über 12 Minuten, letzter Boot bei 25,7 s. Damit sind alle drei
Trigger aus §8.3 abgehakt. Vor dem Fix starb dasselbe Board an derselben
Sequenz nach 5,09 s.

Der Dreifachklick selbst schreibt nichts ins Log — die Zeile `TripleClick` hängt
an `bDisplayCont`. Der GPS-Init ist der Beleg, dass der Handler lief.

### 5.4 Nebenbefund: `--webserver`-Flash über den festgenagelten Port

Der Wechsel auf unseren Stand lief über
`pio run --target upload --upload-port /dev/cu.usbserial-0001` bei gleichzeitig
angeschlossenem T-Beam und RAK. Beide blieben unbehelligt — der Fix aus
Abschnitt 6 wirkt auch im Feld, nicht nur in der Fehlprobe.

---

## 6. Zwischenfall: das falsche Board in den Download-Modus versetzt

Festgehalten als Warnung, nicht als Anekdote.

`upload_command` in `variants/*/platformio.ini` trug **keinen** `--port`. Damit
sucht sich `esptool` den Port selbst. Bei zwei gleichzeitig angeschlossenen
ESP32-Boards griff ein `pio run -e heltec_wifi_lora_32_V3 --target upload` auf
dem Weg den Port des T-Beam ab und schob diesen in den ROM-Download-Modus
(`rst:0x1 (POWERON_RESET), boot:0x3 (DOWNLOAD_BOOT)`). Der Heltec-Flash gelang;
der T-Beam war anschliessend still — der USB-Serial-Wandler meldete sich
weiterhin, der ESP32 nicht mehr, und auch ein EN-Impuls half nicht. Erst
Abziehen und Wiederanstecken der USB-Versorgung brachte ihn zurück.

`--upload-port` half nicht dagegen: ohne `$UPLOAD_PORT` im Kommando hat der
Schalter keine Wirkung, PlatformIO reicht ihn nirgendwohin.

Behoben: alle 27 `upload_command`-Zeilen tragen jetzt `--port "$UPLOAD_PORT"`.
Verifiziert über eine Fehlprobe — `--upload-port /dev/cu.NOSUCHPORT` bricht
jetzt mit genau diesem Portnamen ab, statt sich selbst ein Board zu suchen.

---

## 7. OTA-Update und die Settings-Persistenz

Bis hierher war jeder Flash dieser Sitzung ueber USB gelaufen. Das OTA-Verfahren
selbst war ungeprueft -- fuer ein Release, das Leuten "einfach die firmware.bin
per OTA einspielen" empfiehlt, eine Luecke.

### 7.1 Wer kann OTA, wer nicht

| Board                    | OTA                   | Beleg                                                            |
| ------------------------ | --------------------- | ---------------------------------------------------------------- |
| Heltec V3 (`DK5EN-93`)   | **ja**, `webflash.py` | zweimal durchgefuehrt, Node kommt jeweils sauber zurueck         |
| T-Beam v1.2 (`DK5EN-92`) | **ja**, `webflash.py` | SX1276 und GPS nach dem Update unauffaellig                      |
| RAK4631 (`DK5EN-90`)     | **nein, gar nicht**   | `--ota-update` liegt in `#ifdef ESP32`, keine safeboot-Partition |

Auf dem RAK auf Hardware nachgeprueft: `--ota-update` antwortet
`...wrong command --ota-update`, und der Endpunkt, den das Werkzeug anspricht,
liefert `HTTP 422`. Die Schaltflaeche in der Web-Oberflaeche ist korrekt
`#ifdef ESP32`-geschuetzt (`web_functions.cpp:1088-1094`, dort allerdings
doppelt gesetzt), Benutzer sehen sie also gar nicht erst. nRF52 wird ueber UF2
oder serielles DFU aktualisiert -- das ist Absicht, keine Luecke im Test.

Nebenbefund beim Einrichten: `--wifiap off` schien nicht zu halten. Es ist
kein Fehler -- `esp32_main.cpp:870` erzwingt den AP-Modus, solange das
Rufzeichen noch der Vorgabewert ist, damit ein unkonfigurierter Node immer
erreichbar bleibt. Erst ein echtes Rufzeichen laesst ihn in den Station-Modus
wechseln.

### 7.2 Die Ruecksetz-Regression

Beim OTA-Test bestaetigt: `FLASH_VERSION` ist ein Datum und wurde direkt gegen
`node_fversion` verglichen. Damit hat **jedes** Release mit neuem Datum die
Konfiguration jedes aktualisierenden Knotens geloescht, auch ohne jede
Aenderung an `struct s_meshcom_settings`.

Belegt an `cf7a6676`: der Commit zog `FLASH_VERSION` von 20260724 auf 20260821
und fasste weder `esp32_flash.h` noch `WisBlock-API.h` an.

Getrennt in Build-Kennung (`FLASH_VERSION`) und Layout-Generation
(`FLASH_STRUCT_VERSION`, steht auf 20260724 -- die letzte echte
Layout-Aenderung `6e7c012a`). Nur die Generation entscheidet ueber das
Loeschen; `flashLayoutCompatible()` schuetzt Knoten, die nach alter Semantik
20260821 gespeichert haben.

Auf Hardware, Einstellungen jeweils erhalten:

```
Heltec V3, OTA 1   [INIT]...FLASH layout 20260821 ok, build 20260822
Heltec V3, OTA 2   [INIT]...FLASH layout 20260724 ok, build 20260822
T-Beam,    OTA     [INIT]...FLASH layout 20260821 ok, build 20260822
RAK4631,   DFU     Rufzeichen und Ethernet-Konfiguration erhalten
```

> **Nicht im Release `v4.35p.08.22-stability`.** Dieser Fix ist nach der
> Veroeffentlichung entstanden und bewusst zurueckgestellt; er geht mit dem
> naechsten Release raus. Das veroeffentlichte Binary vergleicht weiterhin
> `FLASH_VERSION`.

---

## 8. Offene Punkte — Laufplan

Bewusst zurückgestellt, nicht stillschweigend übersprungen.

| #   | Punkt                                                                                                                                                                                                                                                                                                                       | Woran es hängt                   |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------- |
| 1   | ~~§8.4 — Board mit `--gps on` und ohne Modul~~ — **erledigt**, Abschnitt 5. Scan läuft durch, keine Phantom-Baudrate, kein Absturz.                                                                                                                                                                                         | —                                |
| 1a  | **B-15 mit verrauschtem Eingang.** Abschnitt 5.2: der freie `GPS_RX_PIN` las null Zeichen, der eigentliche Auslöser von B-15 wurde also nie erzeugt. Belegt ist "meldet keine Phantom-Baudrate", nicht "übersteht Rauschen". Dafür müsste Rauschen eingekoppelt werden.                                                     | Messaufbau, nicht vorhanden      |
| 2   | **§8.4 auf dem RAK** — deckt B-15 nicht ab, der RAK hat seinen eigenen Pfad (N-26)                                                                                                                                                                                                                                          | GPS-Modul am RAK fehlt           |
| 3   | **§8.5 — Batteriestart ohne USB.** Auf dieser Bank **nicht durchführbar**: dem Heltec V3 ist `ARDUINO_USB_CDC_ON_BOOT=1` auskommentiert, der klassische T-Beam hat kein natives USB, der RAK ist nRF52. Es braucht Supreme, V4, wireless_tracker, T3_S3_V1_3, t_deck*, t5_epaper, T-ETH-ELITE oder vision-master-e213/e290. | passendes Board                  |
| 4   | ~~§8.3, Tastendruck~~ — **erledigt**, Abschnitt 5.3. Dreifachklick auf PRG, voller 12-s-Scan aus dem Callback, kein Abbruch.                                                                                                                                                                                                | —                                |
| 5   | ~~T-Beam mit GPS und WiFi~~ — **erledigt**, siehe Abschnitt 4                                                                                                                                                                                                                                                               | —                                |
| 5a  | ~~T-Beam ohne 3D-Fix am Prüfplatz~~ — **erledigt**, Fix nach rund acht Minuten Kaltstart: 8 Satelliten, HDOP 1,1                                                                                                                                                                                                            | —                                |
| 6   | ~~Welle 3 — tote ISR-Variante und A-1…A-9~~ — **erledigt**. Dabei A-9 widerlegt und entdeckt, dass `gps_functions.cpp:24` die Variantenoption `GPS_BAUDRATE_SOFTCHECK` in 15 `configuration.h` ueberschrieben hat.                                                                                                          | —                                |
| 6a  | **15 inerte `GPS_BAUDRATE_SOFTCHECK`-Defines** in `variants/*/configuration.h`. Seit Welle 3 wirkungslos, aber sie lesen sich weiter wie eine Wahl. Eigener Aufraeumlauf.                                                                                                                                                   | noch offen                       |
| 7   | **Welle 4** — S2, die vier unbegrenzten Schleifen (B-1, B-2, B-6, B-10) und der AP-Zweig B-13                                                                                                                                                                                                                               | zurückgestellt                   |
| 8   | **Welle 5** — Coredump-Partition (A-7). Ohne sie gibt es weiterhin `error=257` statt eines Dumps.                                                                                                                                                                                                                           | zurückgestellt, braucht OTA-Test |
| 9   | **Welle 6** — B-1…B-15 als Katalogeinträge                                                                                                                                                                                                                                                                                  | zurückgestellt                   |
| 10  | **N-26 beheben** — N-27 und N-28 sind erledigt und auf Hardware verifiziert. N-26 bleibt: der RAK-GPS-Pfad merkt sich das Fehlschlagen der Erkennung nicht.                                                                                                                                                                 | N-26 offen                       |
| 11  | **WiFi ohne Webserver meldet nichts** (Abschnitt 4.3). Zugangsdaten gesetzt, aber ohne `--webserver on`/`--gateway on` wird `startNetwork()` nie betreten — kommentarlos. Entweder Hinweis ausgeben oder in der Hilfe erwähnen.                                                                                             | neuer Befund, nicht behoben      |
| 12  | **`[GPS_VER]` bleibt auf dem T-Beam leer.** Auf dem Heltec V3 liefert `UBX_MON_VER` eine vollständige Versionszeile, auf dem T-Beam nichts — dort wird das Modul unmittelbar davor auf 38400 umgestellt. Vermutlich ein Rennen, nicht geprüft.                                                                              | Beobachtung, nicht untersucht    |

---

## 9. Was nicht geprüft wurde

- **Keine native Testabdeckung** für den Scan. Abschnitt 8 des Bug-Dokuments
  begründet, warum das ein eigenes Projekt wäre; die Zeiteigenschaft ist hier
  ausschliesslich per Hardware-Mitschnitt belegt.
- **Die gemeldete T-Beam Supreme** stand nicht auf der Bank. Verifiziert wurde
  auf einem Heltec V3 mit demselben Fehlerbild; die Supreme wurde nur gebaut.
- **L76K** wurde nicht geprüft — beide geprüften Module sind u-blox. Der
  L76K-Zweig in `GPSprobe()` ist damit auf dieser Bank unbelegt.
- **Der Funkbetrieb** des T-Beam wurde nicht geprüft. Der SX1276 initialisiert
  und hört zu; ob er sendet und empfängt, wurde nicht gemessen.
