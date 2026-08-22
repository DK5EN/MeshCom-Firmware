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
1 und 2 behoben; die Erkennungsdauer fällt von 12 000 ms auf 2120 ms. Auf dem
RAK4631 ist kein GPS-Modul erreichbar — das ist kein Regress unserer Firmware,
der Upstream-Release verhält sich identisch.

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

| Messung                    | vorher    | nachher     |
| -------------------------- | --------- | ----------- |
| Baudratenerkennung, Boot 1 | 12 000 ms | **2120 ms** |
| Baudratenerkennung, Boot 2 | 12 000 ms | **2140 ms** |
| `--gps reset` im Betrieb   | Absturz   | **2309 ms** |
| `--gps off` + `--gps on`   | Absturz   | **2249 ms** |
| `WZ_GPS_Init()` gesamt     | Absturz   | ~4,9 s      |

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

## 4. Offene Punkte — Laufplan

Bewusst zurückgestellt, nicht stillschweigend übersprungen.

| #   | Punkt                                                                                                                                                  | Woran es hängt                                            |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------- |
| 1   | **§8.4 — Board mit `--gps on` und ohne Modul.** Muss den Scan zu Ende führen und darf **keine** Phantom-Baudrate melden. Direkter Test des B-15-Fixes. | Board ohne GPS am Platz; auf dem nächsten Board einplanen |
| 2   | **§8.4 auf dem RAK** — deckt B-15 nicht ab, der RAK hat seinen eigenen Pfad (N-26)                                                                     | GPS-Modul am RAK prüfen                                   |
| 3   | **§8.5 — Batteriestart ohne USB** auf einem Board mit `ARDUINO_USB_CDC_ON_BOOT=1`. Der eigentliche B-7-Fall, den Welle 0 behebt.                       | Akku vorhanden, Test verschoben                           |
| 4   | **§8.3, Tastendruck** auf einem laufenden Node — der dritte Trigger. `--gps reset` und `--gps on` sind geprüft.                                        | Handgriff am Gerät                                        |
| 5   | **T-Beam mit GPS und WiFi** — bisher ungetestete Plattform                                                                                             | steht als nächstes an                                     |
| 6   | **Welle 3** — tote ISR-Variante und A-1…A-9 löschen, inklusive unseres `pulseIndex + 1`-Regresses                                                      | zurückgestellt                                            |
| 7   | **Welle 4** — S2, die vier unbegrenzten Schleifen (B-1, B-2, B-6, B-10) und der AP-Zweig B-13                                                          | zurückgestellt                                            |
| 8   | **Welle 5** — Coredump-Partition (A-7). Ohne sie gibt es weiterhin `error=257` statt eines Dumps.                                                      | zurückgestellt, braucht OTA-Test                          |
| 9   | **Welle 6** — B-1…B-15 als Katalogeinträge                                                                                                             | zurückgestellt                                            |
| 10  | **N-26/N-27/N-28 beheben**                                                                                                                             | aufgenommen, nicht behoben                                |

---

## 5. Was nicht geprüft wurde

- **Keine native Testabdeckung** für den Scan. Abschnitt 8 des Bug-Dokuments
  begründet, warum das ein eigenes Projekt wäre; die Zeiteigenschaft ist hier
  ausschliesslich per Hardware-Mitschnitt belegt.
- **Die gemeldete T-Beam Supreme** stand nicht auf der Bank. Verifiziert wurde
  auf einem Heltec V3 mit demselben Fehlerbild; die Supreme wurde nur gebaut.
- **L76K** wurde nicht geprüft — das Modul hier ist ein u-blox.
