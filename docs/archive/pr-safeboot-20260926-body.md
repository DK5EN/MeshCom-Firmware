## Ueberblick

Ueberarbeitung des Safeboot-OTA (`src/safeboot/`) plus ein Build-Fix fuer die beiden Safeboot-Envs. Baut auf #1157 auf (inzwischen gemergt): der Safeboot nutzt dessen `makeDhcpHostname()` aus `src/configuration_global.h`. Der Branch sitzt direkt auf dem aktuellen `dev` (6cc8b552), ein Commit.

## Was wurde geaendert

### Safeboot-OTA

- Neu `src/safeboot/ota_state.h`: die OTA-Sitzung als Zustandsautomat (idle, receiving, verifying, done, aborted) mit Abbruchgrund, Generation je Sitzung und vorzeichenbehafteten Zeitdifferenzen (`(int32_t)(now - then)`).
- `src/safeboot/ElegantOTA.cpp`/`.h`: Abschluss nur nach vollstaendiger und verifizierter Uebertragung (fail-closed); Stall-Watchdog; die Generation haengt an jeder einzelnen Upload-Anfrage, ein verspaeteter Abschluss-Handler einer abgeloesten Anfrage beendet die neue Sitzung nicht.
- `src/safeboot/main.cpp`:
  - WLAN-Join nicht blockierend; findet der Knoten kein WLAN, startet nach 25 s ein Auto-AP.
  - Neue Endpunkte `/ota/info`, `/ota/state` (JSON), `/ota/scan`, `/ota/cancel`.
  - `esp_image_verify()` auf `ota_0` beim Start und nach jedem Abbruch, gemeldet als `app_valid`. Ist die App ungueltig, entfaellt der Rueckfall in die App, und `/ota/cancel` wird abgewiesen (409).
  - DHCP-Hostname aus dem Rufzeichen.
- `src/safeboot/ota.html` (+ `ota.h` = `xxd -i ota.html`): Statusseite mit Netzwerk-Panel (Wifi-Client, Auto-AP, Scan mit SSID/RSSI/CH/AUTH/BSSID) und Hinweis bei ungueltiger App; die Seite folgt dem Knoten nach dem Upload zurueck in die App.
- Neu `src/safeboot/safeboot_log.h`: Konsolenausgabe auf UART0 und, sobald ein Host haengt, zusaetzlich auf Native-USB-CDC (S3-Boards). Serielle Marker `[SAFEBOOT];ota;...`.
- Neu `docs/safeboot-ota-contract.md`: der Vertrag fuer `/ota/state`, Zustandsautomat, Abbruchgruende und serielle Marker.
- Neu `test/test_safeboot_state/` und `env:native_safeboot` (nur `-I src`, keine Stubs): 17 Host-Tests fuer den Zustandsautomaten.

### Build-Fix

- Neu `tools/ensure_tasmota_framework.py`, als `pre:`-Skript in `env:esp32-safeboot` und `env:esp32-S3-safeboot`.

## Warum

- **Nur ein App-Slot:** Alle Partitionstabellen mit Safeboot haben genau einen App-Slot (`ota_0`). `Update` schreibt ab dem ersten Block hinein, ein abgebrochener Upload zerstoert also die laufende Firmware. Der bisherige Safeboot fiel nach 180 s trotzdem "in die App" zurueck, `esp_ota_set_boot_partition()` scheiterte (`ESP_ERR_OTA_VALIDATE_FAILED`), und der Knoten startete wieder im Safeboot, alle 180 s im Kreis. Jetzt bleibt der Safeboot stehen und zeigt, dass nur ein vollstaendiger Upload hilft.
- **Falscher Stall-Abbruch:** Ein `millis()`-Delta ueber zwei Tasks hinweg konnte ohne Vorzeichen auf rund 2^32 ueberlaufen und eine gesunde Uebertragung als "stalled" abbrechen.
- **Verspaetete Ereignisse:** Ein spaeter AsyncTCP-Disconnect oder der Abschluss-Handler einer abgeloesten Anfrage konnte eine laufende neue Sitzung beenden oder als fertig melden.
- **Kein Netz, keine Rettung:** Blieb der WLAN-Join haengen, war der Knoten im Safeboot nicht erreichbar. Der Auto-AP macht ihn ohne USB wieder erreichbar.
- **Build-Fix:** Die Safeboot-Envs nutzen die Tasmota-Plattform, deren Paket `framework-arduinoespressif32` sich das Installationsverzeichnis mit dem gleichnamigen, inkompatiblen Paket der Mainline-Plattform teilt. Nach jedem Mainline-Build bricht der naechste Safeboot-Build mit `TypeError: expected str, bytes or os.PathLike object, not NoneType` ab (der zweite Versuch gelingt). Das Skript entfernt das fremde Paket vor dem Builder und installiert das passende.

## Getestet

- Auf dem aktuellen `dev` (6cc8b552) am 26.09. erneut: `pio test -e native_safeboot` 17/17; gebaut `heltec_wifi_lora_32_V3`, direkt danach `esp32-safeboot` und `esp32-S3-safeboot` (erster Anlauf, kein TypeError), dazu `wiscore_rak4631` und `ttgo_tbeam`. Die neuen Endpunkte und Abbruchgruende stehen in beiden gebauten Safeboot-Images.
- `pio test -e native_safeboot`: 17/17.
- Gebaut: `heltec_wifi_lora_32_V3`, danach direkt `esp32-safeboot` (im ersten Anlauf, das Skript greift) und `esp32-S3-safeboot`.
- Hardware, Heltec V3 (DK5EN-1), per USB mit dem neuen Safeboot geflasht, dann die Abbruch-Bank ueber WLAN, 6 von 6 bestanden:
  - `control`: voller Upload, `ota;verify;result;ok`, `ota;end;result;success`, App nach 120 s zurueck.
  - `kill50`: TCP-Verbindung bei 50 % getrennt, Abbruch `client_disconnected`, `app_valid` gemeldet.
  - `stall`: Pause laenger als der Stall-Watchdog, sauberer Abbruch.
  - `doublestart`: zweiter `/ota/start` waehrend der ersten Uebertragung, erste Sitzung `stale_session`, zweite laeuft durch.
  - `cancel`: ohne Upload Rueckfall in die App, waehrend eines Uploads abgewiesen.
  - `control` am Ende erneut.
- Die Kampagne lief am 13.09. zusaetzlich auf T-Beam v1.2 und T-Deck Plus (6/6 je Board, damaliger Fork-Stand).
- Nicht beobachtet: der DHCP-Hostname im Safeboot (kein Log dazu), der Auto-AP in diesem Lauf.

## Hinweise fuer den Reviewer

- `safeboot.bin` und `safeboot-s3.bin` im Repo-Root sind bewusst nicht neu eingecheckt: die CI baut beide bei jedem Tag selbst und veroeffentlicht sie. Wer lokal ein Board per `upload_command` flasht, sollte vorher `esp32-S3-safeboot` bzw. `esp32-safeboot` bauen, sonst landet der alte Safeboot neben der neuen App.
- Ein zweiter App-Slot (echter Rueckfall auf die alte Firmware) braucht eine neue Partitionstabelle und passt nicht auf 4-MB-Boards; nicht Teil dieses PR.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
