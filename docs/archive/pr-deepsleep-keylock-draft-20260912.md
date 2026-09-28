# PR-Entwurf: Long-Press-Deepsleep weckt sofort wieder auf, T-Deck Tastaturlicht bei Keylock

Eingereicht 2026-09-12 als [PR #1140](https://github.com/icssw-org/MeshCom-Firmware/pull/1140) (Branch `pr-deepsleep-keylock-20260912`, drei Commits auf `dev` `c17c07c0`).

Ziel: `icssw-org/MeshCom-Firmware`, Branch `dev`. Drei Feldmeldungen gegen 4.35t, drei Commits,
nur `src/`. Bugreports: `docs/bugreport-heltec-v3-longpress-deepsleep.md`,
`docs/bugreport-tdeck-keylock-kbl.md`.

## 1. Heltec V3 (und alle Long-Press-Deepsleep-Boards) lassen sich per Taste nicht mehr ausschalten

### Symptom

Auf 4.35s schaltet ein langer Druck auf die PRG-Taste (GPIO0) den Node aus, das OLED bleibt
dunkel bis zum Reset. Auf 4.35t geht das OLED kurz aus und der Node bootet sofort wieder
(`[BOOT] RESET_REASON=5 DEEPSLEEP`). Gemeldet fuer Heltec V3; derselbe Pfad gilt fuer Heltec
V2/V4, Wireless Stick V3, Wireless Tracker, TLORA V2.1.6 sowie Heltec T114 und T-Echo (nRF52).

### Ursache

PR #1135 hat den `--deepsleep`-Rumpf in `esp32EnterDeepSleep()` / `nrf52EnterDeepSleep()`
zusammengezogen und dabei erstmals die Bedientaste als Wake-Quelle armiert (ext1 `ANY_LOW` bzw.
`systemOff(pin, LOW)`). `PressLong()` in `src/onebutton_functions.cpp` haengt an
`attachLongPressStart()` und feuert 800 ms nach Druckbeginn, nicht beim Loslassen. Bis zu
`esp_deep_sleep_start()` vergehen dann rund 50 ms, der Finger liegt noch auf der Taste, der Pin
ist LOW, die Wake-Bedingung ist beim Einschlafen bereits erfuellt: der Chip wacht sofort auf und
bootet. Auf 4.35s war schlicht keine Wake-Quelle armiert, deshalb blieb der Node dunkel.

Wireless Paper und E213 armieren die Taste in ihrem eigenen `--deepsleep`-Zweig genauso; dort
ueberbrueckt bisher nur der E-Ink-Refresh (~300 ms) die Zeit bis zum Loslassen.

### Aenderungen

- `src/esp32/esp32_sleep.{h,cpp}`: neu `esp32WaitButtonRelease()`. Liest `iButtonPin`; ist der
  Pin LOW, wird bis zum Loslassen gewartet (begrenzt auf 10 s, dann 100 ms Entprellung). Kein
  Button (99) oder Pin bereits HIGH (`--deepsleep` per Serial/BLE): kehrt sofort zurueck.
  `esp32EnterDeepSleep()` ruft die Funktion direkt vor dem ext1-Armieren, also nach Funk-Sleep und
  Display-aus -- der Node wird weiterhin nach etwa einer Sekunde dunkel, schlaeft aber erst nach
  dem Loslassen.
- `src/command_functions.cpp`: der WP_DISP-Zweig (Wireless Paper, Vision Master E213) ruft
  `esp32WaitButtonRelease()` vor `esp_sleep_enable_ext1_wakeup()`. Ersetzt das bisherige
  Timing-Glueck durch eine explizite Wartebedingung.
- `src/nrf52/nrf52_sleep.cpp`: gleicher Warteblock inline vor `systemOff(iButtonPin, LOW)`,
  unter derselben `PINS_COUNT`-Pruefung wie das Armieren selbst.

Die Alternative, `PressLong()` an `attachLongPressStop()` zu haengen, wuerde das Verhalten des
Long-Press auf jedem Board aendern (nichts passiert, bis der Finger hebt) und den Fall
`--deepsleep` per Serial/BLE bei zufaellig gedrueckter Taste nicht abdecken.

## 2. T-Deck: Tastaturbeleuchtung geht bei aktivem Keylock mit jeder Nachricht an

### Symptom

Keylock aktiv (SYM+K), Tastaturlicht aus: bei jeder eingehenden Nachricht leuchtet die Tastatur
mit Stufe 150 auf, geht nach dem Display-Timeout wieder aus und wiederholt das bei der naechsten
Nachricht.

### Ursache

`tft_on()` in `src/t-deck/lv_obj_functions.cpp` enthaelt seit `583782a9b` (v4.35p
Tastaturlicht-Schalter) eine invertierte Bedingung: `if (node_keyboardlock)
setKeyboardBacklight(150)`. Vorher stand dort `if (!node_keyboardlock) { if (kbd_light_on) ... }`.
Tastatur, Trackball und Touch rufen `tft_on()` nur ohne Keylock (`tdeck_main.cpp`), die
Nachricht (`msg_focus_and_alert()`) ist die einzige Weckquelle, die auch bei Keylock durchkommt --
deshalb zeigt sich der Fehler nur dort.

### Aenderung

- `src/t-deck/lv_obj_functions.cpp`, `tft_on()`: der Block ist entfernt. `resetBrightness()` ->
  `setBrightness()` (`tdeck_helpers.cpp`) synchronisiert das Tastaturlicht bereits aus der echten
  Einstellung `node_kbllightlock`; ein zweiter, an das falsche Flag gebundener Schreibzugriff ist
  ueberfluessig. Ohne Keylock aendert sich nichts: Tastaturlicht an folgt weiter dem Display,
  Tastaturlicht aus bleibt aus.

## 3. T-Beam Supreme bootet mit 4.35t nicht (Haenger in der Display-Init)

### Symptom

4.35t bleibt auf dem T-Beam Supreme (`BOARD_TBEAM_V3`) nach `[INIT]...Auto detecting display:`
stehen: kein LoRa, kein BLE, keine Antwort auf `--info`, Display dunkel. Zwei Knoten betroffen,
4.35s bootet auf denselben Knoten. Bugreport `docs/bug-tbeam-supreme-435t-display-hang.md`.

### Ursache

PR #1135 (Changelog 203) hat das Supreme-OLED von Software-I2C auf Hardware-I2C umgestellt und
dabei die Pins an den u8g2-Konstruktor gegeben
(`U8G2_..._F_HW_I2C(U8G2_R0, U8X8_PIN_NONE, 18, 17)`). Mit expliziten clock/data-Pins fuehrt
u8g2 in `U8X8_MSG_GPIO_AND_DELAY_INIT` (`U8x8lib.cpp`) `pinMode(pin, OUTPUT)` auf beiden Pins
aus; `pinMode` haengt den Ausgang ueber `gpio_config` auf `SIG_GPIO_OUT_IDX` um und trennt die
Pins vom I2C-Controller, der sie seit `Wire.begin(17, 18)` im Setup besitzt. Das anschliessende
`Wire.begin(17, 18)` aus u8g2 ist auf dem laufenden Bus ein No-op ("bus already initialized"),
der Controller startet die Init-Sequenz auf einem Bus, den er nicht mehr treibt, und
`u8g2->begin()` haengt. Ein Diagnose-Build zeigt das Panel unmittelbar davor mit ACK auf 0x3C.
Der Supreme ist das einzige S3-Board mit diesem Muster; Heltec V3/V4/Stick V3 nutzen den
Konstruktor ohne Pins plus `Wire1.setPins()`, T-Beam v1.2 und RAK den Konstruktor ohne Pins.

### Aenderungen

- `src/loop_functions.cpp`, Zweig `BOARD_TBEAM_V3`: beide Konstruktoren ohne Pins
  (`U8G2_R0` allein). u8g2 bleibt damit bei `Wire.begin()` ohne Argumente und fasst keine GPIOs
  an.
- `src/esp32/esp32_functions.cpp`, `initDisplay()`: `u8g2->setBusClock(100000)` vor
  `u8g2->begin()` fuer `BOARD_TBEAM_V3`. u8g2 wuerde fuer den SH1106 sonst 400 kHz setzen; der
  Sensorpfad (BME280) auf demselben Bus faehrt 100 kHz. Ein Bild kostet damit rund 100 ms statt
  570 ms mit Software-I2C, der Gewinn aus Eintrag 203 bleibt.

## Nachweis

Build: alle betroffenen Envs sauber (Heltec V3, T-Deck Plus, RAK4631, Wireless Paper, Vision
Master E213, Heltec T114, T-Echo, T-Beam classic, T-Beam Supreme).

T-Beam Supreme: kein Board auf der Bench, Fix nur compile-verifiziert. Der Diagnose-Build mit
Markern hat den Haenger in `u8g2->begin()` und den intakten Bus davor gezeigt (siehe Bugreport);
die Feldbestaetigung des Fix-Builds steht aus.

Heltec V3 (DK5EN-93), `--deepsleep` ueber Serial bei unberuehrter Taste: Node schlaeft sofort
(letzte Ausgabe `Disbling Vext`), kein Reboot in 15 s, Wecken per Reset. Long-Press mit gehaltener Taste (`--button on`), zwei Zyklen am Stueck: `GO to deepsleep` um 21:36:20, Node dunkel und dunkel nach dem Loslassen, Wecken per Tastendruck um 21:36:25 mit `RESET_REASON=8 DEEPSLEEP`, zweiter Long-Press um 21:36:35, Node bleibt dunkel. Auf 4.35t bootete derselbe Knoten sofort wieder.

RAK4631 (DK5EN-90), `--deepsleep` ueber Serial: System OFF (USB-Port verschwindet). Ohne die
`bButtonCheck`-Bedingung dauerte das 12,6 s -- WB_IO6 ist bei `--button off` nie konfiguriert
und liest LOW, die Wartebedingung lief bis zur 10-s-Grenze. Mit der Bedingung: sofort.

T-Deck Plus (DK5EN-14), Bench-Szenario `keylock_kbl` (Keylock per SYM+K-Tastencode, Nachricht
per `--injectmsg`, Beobachtung der Schreibzugriffe auf den Tastatur-Controller):

| Stand   | Panel weckt | Tastaturlicht-Schreibzugriff nach der Nachricht |
| ------- | ----------- | ----------------------------------------------- |
| vorher  | ja          | `150` (der Fehler)                              |
| nachher | ja          | keiner                                          |

Handtest auf demselben Geraet 2026-09-12: KBL aus, SYM+K, Nachricht per LoRa -> Display weckt,
Tastatur bleibt dunkel, SYM+K entsperrt wieder.
