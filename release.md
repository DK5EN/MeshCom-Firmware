# Release Notes -- MeshCom Firmware v4.35* (2026-03-22)



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

### ESP DevKits + E22 LoRa Modul
- E22-DevKitC.bin (433 MHz)
- E22_XML-DevKitC.bin (433 MHz)
- E22_1268_S3-DevKitC.bin (433 MHz)
- E22_1262-DevKitC.bin (868 MHz)
- E22_1262_S3-DevKitC.bin (868 MHz)

### ESP32 Lora-Aprs
- esp32-loraprs-e22
- esp32-loraprs-ra01

### HELTEC
- heltec_wifi_lora_32_V2.bin
- heltec_wifi_lora_32_V3.bin
- heltec_wifi_lora_32_V4.bin
- heltec_wireless_stick_v3.bin
- heltec_wireless_tracker.bin
- heltec_t114.zip, .uf2

### Lilygo
#### TBEAM
- ttgo-lora32-v21.bin
- ttgo_tbeam.bin
- ttgo_tbeam_SX1262.bin
- ttgo_tbeam_SX1268.bin
- ttgo_tbeam_supreme_l76k.bin
- ttgo_tbeam_1W.bin

#### E-PAPER
- vision-master-e290.bin
- vision-master-e213.bin

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

### Newer version > v4.35 able to upgrade via OTA-Flasher.

### [MeshCom Changelog](https://icssw.org/meshcom-versionen/)

### [MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
