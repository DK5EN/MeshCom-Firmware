# Release Notes -- MeshCom Firmware v4.35p

Firmware `4.35p`, `FLASH_VERSION 20260821` (`src/configuration_global.h`).
Aeltere Eintraege bis einschliesslich 2026-03-22 stehen im Archiv
[`docs/release_lora_trx.md`](docs/release_lora_trx.md).

---

## Stability-Release v4.35p.08.21-stability (2026-08-21)

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

- `T-ETH-ELITE_1262` -- steht in `default_envs` und wird im CI gebaut, ist aber
  nicht in der Artefaktliste des Release-Workflows.
- `t5_epaper`, `vision-master-e213-preview`, `esp32-external-radio` -- Opt-in,
  nicht in `default_envs`.

### Newer version > v4.35 able to upgrade via OTA-Flasher.

### [MeshCom Changelog](https://icssw.org/meshcom-versionen/)

### [MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
