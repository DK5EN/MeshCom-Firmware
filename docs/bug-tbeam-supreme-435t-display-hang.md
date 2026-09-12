# Bug: T-Beam Supreme bootet mit 4.35t nicht (Haenger in der Display-Init)

Stand: 2026-09-12, Diagnose abgeschlossen, Fix gebaut, Feldbestaetigung offen.

Fix in `fork-main` als `1427ac6f` (nur Konstruktor und `setBusClock`, ohne Diag-Marker), Changelog
220, BACKLOG TM-09; im Upstream-PR zusammen mit DS-03/TD-16 (`pr-deepsleep-keylock-draft-20260912.md`).

## Symptom

- Firmware 4.35t (upstream DEV, Merge von PR #1135) bleibt auf dem T-Beam Supreme
  (`BOARD_TBEAM_V3`, env `ttgo_tbeam_supreme`) nach `[INIT]...Auto detecting display:`
  stehen. Kein LoRa, kein BLE, keine Antwort auf `--info`, Display bleibt dunkel.
- 4.35s (Fork-Build vom 2026-09-03, FLASH layout 20260724) bootet auf demselben Knoten.
- Zwei Knoten betroffen (GW_ID 435A7140 beim Tester, 435750D8 im Studio), also kein
  Einzelgeraet.
- Nebeneffekt bei der Diagnose: der Supreme hat natives USB, der S3 verwirft Boot-Ausgabe,
  die vor dem Oeffnen des Ports gedruckt wurde. Ein haengender Knoten wirkt deshalb
  "komplett tot" auf der Seriellen, obwohl er bis zur Display-Init gekommen ist.

## Eingrenzung

Das Upstream-Delta 4.35s -> 4.35t ohne unseren PR umfasst fuenf Dateien
(Versionsstring, `idf_component.yml.orig`, `nrf52_sleep.cpp`, zwei Safeboot-Binaries),
nichts davon betrifft den Supreme. Die einzige Supreme-spezifische Aenderung in PR #1135
ist Changelog-Eintrag 203 (Commit 0b0c0984, 2026-09-06): OLED von Software-I2C
(`_1_SW_I2C`) auf Hardware-I2C (`_F_HW_I2C`) auf `Wire`, Pins 17/18. Im BACKLOG (TM-09)
als "compile-verified only, no Supreme board on the bench" vermerkt.

## Diagnose-Build und Befund

Marker (`Serial.printf` + `flush`) um `u8g2->begin()` und `startDisplay()`, dazu eine
Bus-Probe auf 0x3C vor der Display-Init. Log vom Studio-Knoten:

```
[INIT]...Auto detecting display:
[DIAG];probe;0x3C;clock=100k;ack=0
[DIAG];before;u8g2.begin
```

- Der Bus ist vor der Display-Init intakt: das Panel antwortet auf 0x3C mit ACK bei
  100 kHz ueber den Hardware-Controller.
- Der Haenger liegt in `u8g2->begin()`.
- Die Zeilen `[DIAG];pins;...` und `[DIAG];probe;...;clock=400k;...` fehlen im Log. Das
  Studio-Terminal verschluckt Zeilen (auch `RTC not found[OBUT]...` ist zusammengezogen),
  daher ist der 400-kHz-Befund nicht auswertbar.

## Ursache

Zwei Kandidaten im Code, beide nur beim Supreme, beide im Fix abgeraeumt.

### 1. Explizite I2C-Pins im u8g2-Konstruktor (Hauptkandidat)

`src/loop_functions.cpp`, Zweig `BOARD_TBEAM_V3`:

```
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2_2(U8G2_R0, U8X8_PIN_NONE, 18, 17);
```

Mit gesetzten clock/data-Pins fuehrt u8g2 in `U8X8_MSG_GPIO_AND_DELAY_INIT`
(`U8x8lib.cpp:97-105`) ein `pinMode(pin, OUTPUT)` auf beiden Pins aus, weil
`U8X8_PIN_I2C_CLOCK` (12) und `U8X8_PIN_I2C_DATA` (13) unter `U8X8_PIN_OUTPUT_CNT` (16)
liegen. `pinMode` auf ESP-IDF 4.4 geht ueber `gpio_config`, das den Ausgang auf
`SIG_GPIO_OUT_IDX` umhaengt und die Pins damit vom I2C-Controller trennt. Das
anschliessende `Wire.begin(17, 18)` von u8g2 ist ein No-op, weil `Wire` seit
`esp32_main.cpp:710` auf denselben Pins laeuft (arduino-esp32 2.0.14,
`Wire.cpp:134-137`: "bus already initialized"). Der Controller startet die
Init-Sequenz auf einem Bus, den er nicht mehr treibt.

Der Supreme ist das einzige S3-Board mit diesem Muster. Heltec V3/V4 und Stick V3
nutzen den Konstruktor ohne Pins und `Wire1.setPins()`, T-Beam v1.2 und RAK den
Konstruktor ohne Pins. Der Heltec V2 (klassischer ESP32) hat dasselbe Muster mit
Pins 4/15 und laeuft, der S3-I2C-Controller verhaelt sich bei weggenommenem Bus aber
anders als der klassische.

### 2. Bus-Takt 400 kHz

u8g2 setzt fuer den SH1106 `i2c_bus_clock_100kHz = 4`, also 400 kHz, vor jedem
Transfer (`U8x8lib.cpp:1368`). Der Sensorpfad auf demselben Bus (BME280 an 0x77,
`MC_I2C_NEEDS_BUS_RESET`) faehrt 100 kHz. Ob der Bus des Supreme 400 kHz vertraegt, ist
wegen der fehlenden Logzeile offen. Ein NACK bei 400 kHz wuerde eher ein dunkles Display
als einen Haenger erzeugen, der Kandidat ist also nachrangig, aber nicht ausgeschlossen.

## Fix (gebaut, noch nicht im Feld bestaetigt)

`src/loop_functions.cpp`, Zweig `BOARD_TBEAM_V3`:

```
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_1(U8G2_R0);
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2_2(U8G2_R0);
```

`src/esp32/esp32_functions.cpp`, `initDisplay()`, vor `u8g2->begin()`:

```
#if defined(BOARD_TBEAM_V3)
u8g2->setBusClock(100000);
#endif
```

Ohne Pins bleibt u8g2 bei `Wire.begin()` ohne Argumente (No-op auf dem laufenden Bus)
und fasst keine GPIOs an. 100 kHz passt zum Sensorpfad; ein Bild kostet damit rund
100 ms statt 570 ms mit Software-I2C. Ob 400 kHz spaeter wieder moeglich ist, entscheidet
die Probe-Zeile aus einem vollstaendigen Log.

Build auf dem Desktop: `MeshCom-Firmware/firmware_tbeam_supreme_fix1.bin` (OTA) und
`firmware_tbeam_supreme_fix1_FULL.bin` (USB ab Offset 0). Die Diag-Marker sind noch
enthalten, erwartete letzte Marker-Zeile:

```
[DIAG];after;startDisplay.nextPage;frame_ms=...
```

## Offen

- Feldbestaetigung des Fix-Builds: Boot bis `[LoRa]...`, Startbild am Display, `--info`
  antwortet, keine `[INSTR-LOOP] gap ... display_tick` mehr.
- `frame_ms` aus dem Log fuer den Changelog-Eintrag.
- Vollstaendiges Log mit einem anderen Terminal als Studio, damit die Pins- und die
  400-kHz-Zeile sichtbar werden.
- Vor dem Einbau in fork-main: Diag-Marker in `esp32_functions.cpp` entfernen, nur der
  Konstruktor und `setBusClock` bleiben. Changelog-Eintrag 203 ergaenzen, BACKLOG TM-09
  von "compile-verified only" auf den Feldbefund umschreiben.
- Upstream-PR gegen DEV mit deutscher Beschreibung, Bezug auf PR #1135 / Eintrag 203.

## Flash-Hinweis Supreme

Die Fork-Partitionierung legt die App bei 0xC0000 (Safeboot bei 0x10000). Ein Knoten
mit offizieller Upstream-Firmware hat die App bei 0x10000; ein Einzel-Image bei 0xC0000
ueberschreibt dann die laufende App und der Knoten ist tot bis zum USB-Flash des
FULL-Images. Knoten mit Fork-Firmware (ab v4.35k) nehmen das App-Image per OTA.
