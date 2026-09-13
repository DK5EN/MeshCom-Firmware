# PR-Entwurf: ESP32: Aufwachgrund nach Deepsleep im Boot-Log

Eingereicht 2026-09-13 als [PR #1143](https://github.com/icssw-org/MeshCom-Firmware/pull/1143) (Branch `pr-wake-cause-20260913`, ein Commit auf `dev` `893cffd0`).

Ziel: `icssw-org/MeshCom-Firmware`, Branch `dev`. Ein Commit, nur `src/esp32/esp32_main.cpp`.

## Motivation

Seit PR #1135 und #1140 gibt es den Deepsleep auf allen ESP32-Boards und den Aufweckweg ueber
die Taste (ext1). Im Boot-Log steht bisher nur `[BOOT] RESET_REASON=5 DEEPSLEEP`; ob der Knoten
durch die Taste, einen Timer oder etwas anderes aufgewacht ist, laesst sich aus dem Log nicht
ablesen. Bei Feldmeldungen ("Knoten wacht von selbst auf", "Taste weckt nicht") fehlt damit die
eine Zeile, die den Fall entscheidet.

## Aenderung

- `src/esp32/esp32_main.cpp`: neue statische Hilfsfunktion `wakeCauseName()` bildet
  `esp_sleep_source_t` auf den Namen ab (`EXT0`, `EXT1`, `TIMER`, `TOUCHPAD`, `ULP`, `GPIO`,
  `UART`, sonst `OTHER`). In `esp32setup()` direkt hinter der `RESET_REASON`-Zeile, nur wenn der
  Reset-Grund `ESP_RST_DEEPSLEEP` ist:

  ```
  [BOOT] WAKE_CAUSE=3 EXT1
  ```

  Raw `Serial.printf` wie die `RESET_REASON`-Zeile daneben, also unabhaengig von `--debug`.
  `#include <esp_sleep.h>` kommt dazu. Auf einem Kaltstart aendert sich nichts.

- Im selben Commit: der Kommentar am `gpio_hold_dis(PIN_LORA_NSS)` im Wake-Pfad (Wireless
  Paper / Vision Master E213) traegt jetzt die Feldbestaetigung von OE3LCR vom 2026-09-11 aus
  der Diskussion zu PR #1135 (EXT1-Wake, Funkinitialisierung und SPI-Verkehr in Ordnung) statt
  des alten "nicht auf Hardware verifiziert".

## Nachweis

Build auf `dev` `893cffd0` + Commit: `heltec_wifi_lora_32_V3`, `t_deck_plus` sauber.

Heltec V3 (DK5EN-93), langer Tastendruck bis Deepsleep, dann Taste zum Aufwecken, seriell:

```
[BOOT] RESET_REASON=5 DEEPSLEEP
[BOOT] WAKE_CAUSE=3 EXT1
```

Zwei Laeufe zu je zwei Zyklen am 2026-09-13 mit derselben Zeile (`RESET_REASON=5 DEEPSLEEP`,
`WAKE_CAUSE=3 EXT1`, Funk danach lebendig); Kaltstart und `--reboot` zeigen wie bisher nur
`RESET_REASON`.
