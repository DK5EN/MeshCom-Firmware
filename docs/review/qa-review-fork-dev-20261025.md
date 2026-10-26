# QA-Review fork-dev 2026-10-25: Stabilität, Angriffsfläche, Firmware-Größe

Stand: `fork-dev` f0a81c8 (= `upstream/dev` 7a1cdd70 + Fork), Release `v4.40a.10.25`. Reiner Report, keine
Code-Änderung. Auftrag: Produktion stabil halten, DRY-Verletzungen, Firmware je Env verkleinern (HW-Gating,
DRY, Lib-/Compiler-Flags), adversarialer Review (LoRa-RX, Web/UDP/BLE, OTA/AU/Safeboot, Langzeit).

**Methode.** Orchestrator plus 7 Sonnet-Reviewer (Stabilität, LoRa-RX, Netz/BLE, OTA/AU, DRY, HW-Gating/Flags,
Size-Map). Die vorgesehene Haiku-Leseebene stand den Subagents nicht zur Verfügung (kein Agent-Tool in
Subagents); die Sonnet-Agents haben selbst gelesen. Alle 32 CI-Envs (`default_envs`, inkl. 2× Safeboot) sind
gebaut (`MC_AU_DEV_CHANNEL=1`, 32/32 grün); ELF-Attribution per `nm -l`/DWARF/Literal-Pool (ESP32) bzw. Map
(nRF52). Delta-Messungen ohne pio-Neubau des Repos (eigene Toolchain-Läufe am V3/T-Deck/T114) plus ein
pio-Delta-Build an E22-DevKitC. Kein Hardware-Test.

**Verifikation.** Befunde mit ✔ hat der Orchestrator selbst am Code nachgelesen (Pfad Eingang→Senke). Alle
anderen sind vom jeweiligen Reviewer gegen den Code geprüft, aber nicht doppelt.

**IDs.** Neues Präfix `QA-`, weil `RX-`, `NET-`, `STAB-` im Repo schon belegt sind. Die Rohberichte in
[qa-20261025/](qa-20261025/) nutzen die Kurz-IDs ohne `QA-` (`STAB-01` = `QA-STAB-01`, `AU-R-01` = `QA-AU-01`,
`DRY-R01` = `QA-DRY-01`, `M01` = `QA-SIZE-M01`).

---

## 0. Kurzfassung

**Sofort relevant für DK5EN-98 (Prod-Gateway, Heltec V3):**

1. **QA-NET-04 ✔** Ein UDP-Datagramm ≥255 B an den Gateway-Port legt den Server-Downlink (GATE/BEAT/CONF) bis
   zum Reboot lahm. `Udp.read(…, 254)` ohne `flush()`; `WiFiUDP::parsePacket()` liefert 0, solange Restbytes im
   `rx_buffer` liegen (Framework `WiFiUdp.cpp:208`). Auch ein langes Server-Frame genügt. Fix: 3 Zeilen, Muster
   existiert in `extudp_functions.cpp:493`.
2. **QA-STAB-02** Server-DNS nur einmal pro Boot; Stage-2-Timeout bei WLAN connected loggt nur „waiting". IP-Wechsel
   des Servers = Uplink tot bis Reboot. Zusammen mit QA-NET-04: der Gateway-Watchdog heilt nichts, solange WLAN steht.
3. **QA-STAB-01 ✔** `startExternUDP()` ruft `WiFi.hostByName()` synchron im loopTask (bis 15–16 s,
   `WiFiGeneric.cpp:1572/1578`) gegen Task-WDT. Mit Hostname als `--extudpip` und fehlendem DNS: WDT-Reboot,
   wiederholt bei jedem Pass.

**Höchste Angriffsfläche:**

4. **QA-NET-03 / QA-AU-02 ✔** CSRF-Kette: `/callfunction/?otaupdate` (und `?reboot`) laufen ohne X-MC
   (`web_guard.h:785` sperrt nur `upd*`), `MC_WEB_GUARD` Default 0. Danach Safeboot mit offenem AP und OTA ohne
   Auth (QA-AU-03). Drive-by aus dem Browser eines LAN-Nutzers bis Fremdfirmware.
5. **QA-NET-02 ✔** nRF52: Settings-Characteristic `SECMODE_OPEN` READ/WRITE enthält `node_passwd` (RM1, Netconsole,
   `{MCP}`), `bt_code`, `node_webpwd`, WLAN-PSK im Klartext (`nrf52_ble.cpp:456`, `ble_settings_v1.cpp:115-132`).
6. **QA-NET-01 ✔** BLE-PIN wirkungslos: `readPhoneCommand()` dispatcht 0x50/0x55/0xA0/0xF0 ohne `isPhoneReady`
   (das Flag sperrt nur Notify), also `--`-Kommandos inkl. `--cleanflash`, `--ota-update` ohne PIN.
7. **QA-AU-01 ✔** Safeboot löscht ota_0, bevor die Inflate-Länge geprüft ist; `ilen` stammt aus dem separaten
   `.bin`-Asset (`fw_update_net.cpp:822`), nicht aus dem SHA-geschützten `.autoupdate`. Mismatch → `keep=false`,
   Knoten bleibt ohne App in Safeboot (kein Auto-Rückfall). Auslöser: inkonsistentes Release (QA-AU-05).

**Größe, größte Hebel (gemessen oder kalibriert):**

| Hebel | Envs | Ersparnis | Risiko |
|---|---|---|---|
| QA-SIZE-05 Ethernet+Web auf nRF52 ohne ETH-HW gaten | T114, t_echo | 192–204 KB (≈29 % Image) | mittel (Betreiberentscheid) |
| QA-SIZE-01 `-fno-exceptions` (kein try/catch im Baum) | 27 ESP32-Apps | 106–130 KB | niedrig/mittel |
| QA-SIZE-02 `CORE_DEBUG_LEVEL=0` | 26 ESP32-Apps | 34–46 KB | niedrig |
| QA-SIZE-M01 Safeboot `ota.html` gzip ✔ | 2 Safeboot | 24,9 KB (classic: 7 → 32 KB Luft) | niedrig |
| QA-SIZE-07a/b LVGL-Widgets, TFT_eSPI-Fonts | t_deck, t_deck_plus | 56,7 KB | niedrig |
| QA-SIZE-14 TinyUSB nur CDC | 3 nRF52 | 11 KB | niedrig |

Engpässe: **Safeboot classic (1 % Luft)**, **wiscore_rak4631 (13,5 %)**, **t_echo (16,7 %)**, **heltec_t114
(19,7 %)**. Dazu kommt **QA-SIZE-AU01**: Bei den ESP32-Apps ist nicht der Flash-Headroom (41–45 %) die Grenze,
sondern das AU-Staging, also App plus `.autoupdate` in ota_0. Die T-Beam-Familie verträgt nur noch **≈61 KB** App-Wachstum, dann
verweigert das Auto Update (§3.3). QA-SIZE-01 + 02 sind am E22 per pio gemessen: −139 KB.

---

## 1. Produktions-Stabilität (Langzeit)

Bericht: [qa-20261025/stability.md](qa-20261025/stability.md). Gesamtbild: millis()-Idiome fast überall
differenzbasiert, Tabellen statisch begrenzt, WDT-/Heap-Themen weitgehend gefixt. Offene Punkte:

| ID | Schwere | Stelle | Pfad / Zeitskala | Fix-Richtung |
|---|---|---|---|---|
| QA-NET-04 ✔ | Hoch | `udp_functions.cpp:177`, `gateway_service_esp32.cpp:56-66` | Datagramm ≥255 B → `rx_buffer` bleibt belegt → `parsePacket()`=0 für immer; HB-Timeout bei WLAN ok nur „waiting". ESP32-Gateways, sofort | `if(packetSize>len) Udp.flush();` |
| QA-STAB-01 ✔ | Hoch | `extudp_functions.cpp:194`, Aufruf `esp32_main.cpp:5251` | blockierendes DNS (≤16 s) im loopTask, WDT 5 s; nur mit Hostname in `--extudpip`. Ohne WDT: Ziel 0.0.0.0 gelatcht | `wifiDnsStart`-Muster (async, F6) wiederverwenden; bei Fehlschlag nicht latchen |
| QA-STAB-02 | Mittel-Hoch | `udp_functions.cpp:556-562, 1348`; `gateway_service_esp32.cpp:56-66` | Server-IP-Wechsel → Uplink tot bis Reboot (Tage–Wochen) | bei Stage-2-Timeout re-resolven, oder TTL 6–24 h |
| QA-STAB-03 | Mittel | `platformio.ini:1658` (`NIMBLE_HOST_TASK_STACK_SIZE=3072`), `printfdeb_functions.cpp:90-95` | `printfdeb` (≈1 KB Frame) in NimBLE-Callbacks, keine HWM-Messung | HWM messen; Logs in den Loop verlagern oder 4096 |
| QA-STAB-04 | Mittel | `extudp_functions.cpp:357/626`, `aprs_functions.cpp:684`, `lora_functions.cpp:1585/2353/2504`, `txring_functions.cpp:169` | Per-Paket-Heap-Churn (JsonDocument, `String`-Kopien) bei 67–72 KB Heap; Wochen | `const char*`-Signaturen, snprintf |
| QA-STAB-05 | Niedrig | `udp_functions.cpp:833` | `a > b` zweier Timestamps; einziger Fall im Baum, nach 49,7 d | `(int32_t)(a-b) > 0` |
| QA-STAB-06 / QA-NET-10 | Niedrig | `net_console.cpp:158-186, 495` | 1 Byte/29 s hält den Handshake-Slot ≈35 min; `accept()`-Log ohne Rate-Limit | Gesamtfrist 10 s |
| QA-NET-07 | Mittel | `web_functions.cpp:1186-1200` | `web_currentLine` unbegrenzt; Slowloris: jede Verbindung blockiert den Loop bis 2 s | auf 1023 kappen, Limit je IP |
| QA-AU-09 | Mittel | `esp32_main.cpp:2958`, `web_nodefunctioncalls.cpp:101-117` | Batterie-Floor nur beim Download, nicht beim manuellen Apply | Floor vor Handover |

## 2. Adversarial: Angriffsfläche

### 2.1 LoRa-RX (nicht authentisiert)

Bericht: [qa-20261025/lora_rx.md](qa-20261025/lora_rx.md). **Kein Memory-Corruption-Befund** in `decodeAPRS`,
Encoder, NBR, msgstore, KISS, Externradio (Grenzen und Kappen geprüft). Schwächen sind semantisch: FCS = Bytesumme,
msg_id Klartext und vorhersagbar, Rufzeichen selbstbehauptet.

| ID | Schwere | Stelle | Angriff | Status |
|---|---|---|---|---|
| QA-RX-01 | Krit | `lora_functions.cpp:2182`, `loop_functions.cpp:2642-2722` | `{MCP}` → `--setout` (nur MCP23017-Knoten); PW-Prüfung ist ein Zeichenmengen-Test | bekannt N-01 (WONTFIX) |
| QA-RX-02 | Hoch | `loop_functions.cpp:2724-2761` | `{SET}` 1↔6 im Wechsel → `save_settings()` je Paket: Flash-Wear + Loop-Blockade netzweit | N-02 bekannt, Wear neu |
| QA-RX-03 ✔ | Hoch | `lora_functions.cpp:1074-1124` | Ring-Scan läuft auch bei fehlgeschlagenem Decode: 5 Byte mit passender msg_id stoppen die Wiederholung einer eigenen Nachricht; `:ackNNN`/Binär-ACK ohne Absenderprüfung | F5-5 bekannt, Variante ohne FCS neu |
| QA-RX-04 | Hoch | `dedup_functions.cpp:23-52` | Dedup nur über msg_id (`GWID<<10\|Zähler`): Vorab-IDs zensieren eine Station; nach 70–100 Frames Replay | F5-5 B bekannt |
| QA-RX-05 | Hoch | `lora_functions.cpp:2491, 657, 2311` | kein Rate-Limit je Quelle, Hop-Nibble bis 15 nicht geklemmt, Fremd-ACK-Relay mit Prio CRITICAL | F5-9 bekannt, Details neu |
| QA-RX-06 | Mittel | `lora_functions.cpp:1797`, `hey_policy.h:14-21` | 2 fremde HEY je Intervall unterdrücken das HEY aller Nicht-Gateways | neu |
| QA-RX-07 | Mittel | `aprs_functions.cpp:273-291`, `regex_functions.cpp:22-47` | Sonderziel-Whitelist (`*`, `TEST` …) gilt auch für die Quelle → DM von `*` erzeugt `:ack` als Broadcast (Reflexion) | neu, plausibel |
| QA-RX-08 | Mittel | `loop_functions.cpp:2766-2795` | `{CET}` stellt die Uhr ohne Validierung (nur Jahr>2023) | F5-10 bekannt |
| QA-RX-09 | Mittel | `remote_cmd.cpp:253, 529` | RM1: unsalted SHA-256, 64-Bit-Tag, `rmstrict` Default 0, ein Passwort für RM/Konsole/`{MCP}` → Offline-Raten | teils bekannt |
| QA-RX-10..13 | Niedrig | siehe Bericht | HN-Bericht ohne Auth, `:rej`-Offset (Index −1+4, Upstream-Code), Relay-Truncation, C1-Bytes | neu/bekannt |

### 2.2 Web, UDP, BLE, Netconsole, RM1

Bericht: [qa-20261025/net_ble.md](qa-20261025/net_ble.md). XSS aus Funkdaten ist sauber (`htmlEscape` überall),
Production-Setting-Lock ohne Umgehung gefunden.

| ID | Schwere | Kanal | Stelle | Kern |
|---|---|---|---|---|
| QA-NET-01 ✔ | Hoch | BLE ESP32+nRF52 | `phone_commands.cpp:266-345` | Dispatch ohne `isPhoneReady`-Gate; PIN-Hash statisch, wiederholbar, `memcmp` nicht zeitkonstant |
| QA-NET-02 ✔ | Hoch | BLE nRF52 | `nrf52_ble.cpp:456`, `ble_settings_v1.cpp:115-132/320-337` | Settings-Blob mit allen Geheimnissen offen lesbar und schreibbar (→ `save_settings`) |
| QA-NET-03 ✔ | Hoch | Web/LAN | `web_guard.h:86/785`, `web_nodefunctioncalls.cpp:39-42` | CSRF: `otaupdate`/`reboot` ungeschützt, CORS `*` auch auf `/config.json` |
| QA-NET-04 ✔ | Hoch | UDP Gateway | siehe §1 | Downlink-Wedge |
| QA-NET-05 | Mittel | UDP | `udp_frame_esp32.cpp:119/748/688` | GATE/BEAT ohne Quellprüfung → LAN-Host sendet über das Gateway auf LoRa | bekannt F5-16 |
| QA-NET-06 | Mittel | EXTUDP | `extudp_functions.cpp:475-505` | Eingang ohne Quell-IP-Prüfung → Funk unter eigenem Rufzeichen |
| QA-NET-07 | Mittel | Web | siehe §1 | Header-Zeile unbegrenzt, Slowloris |
| QA-NET-08 | Mittel | Web-Login | `web_functions.cpp:413-470` | keine Sperre, Session = Quell-IP, PW im GET | bekannt |
| QA-NET-09 ✔ | Niedrig | Web | `web_functions.cpp:990` | ohne Web-PW: Logout schreibt `web_ip_passwd_time[-1]=0` (UB, 4 B vor dem Array); Login-Pfad ist geguarded |
| QA-NET-10..14 | Niedrig–Mittel | Konsole, RM1, Web, BLE, AP | siehe Bericht | Handshake-Slot, RM-Offline-Raten, Self-XSS `uic_input`, `delay(1000)` im BLE-Callback, offener AP |
| QA-CMD-01 ✔ | Niedrig | alle Kommandokanäle | `command_functions.cpp:1235, 2891` (+6 Kopien) | `--setlog ` ohne Argument: `_owner_c[strlen-1]` = Index `SIZE_MAX` (OOB-Read, bedingter 1-Byte-Write); über BLE ohne PIN erreichbar (QA-NET-01) |

### 2.3 OTA, Auto Update, Safeboot

Bericht: [qa-20261025/ota_au.md](qa-20261025/ota_au.md). Kein `setInsecure`, SHA-256 gegen API-Digest Pflicht,
CRC vor Erase, Auto-Install physisch aus. Offene Punkte:

| ID | Schwere | Stelle | Kern |
|---|---|---|---|
| QA-AU-01 ✔ | Hoch | `safeboot/main.cpp:576-600, 660-675`; `fw_update_net.cpp:822` | Erase vor Inflate-Längenprüfung; `ilen` aus ungeschütztem Nachbar-Asset → App weg, Knoten bleibt in Safeboot. Fix: Trockenlauf-Inflate vor dem Erase, `ilen` in den `.autoupdate`-Header |
| QA-AU-02 ✔ | Hoch | `web_guard.h:785` | `otaupdate` nicht unter X-MC (= QA-NET-03) |
| QA-AU-03 | Hoch | `safeboot/main.cpp:267/765/962`, `ElegantOTA.cpp` | Safeboot-OTA ohne Auth, AP offen, `/ota/start` per GET, keine Board-Prüfung | teils bekannt (deklarierte Ausnahme) |
| QA-AU-04 | Mittel | Safeboot | Board-Marker `MCBOARD1` wird nie gegen `rec.env` erzwungen (S3-Board A ↔ B) |
| QA-AU-05 | Mittel | `meshcom-ci.yml:100-190` | Release geht Latest vor vollständigem Upload; kein Paar-Check `.bin`↔`.autoupdate`/Marker (Konfidenz mittel) |
| QA-AU-06/07 | Mittel | AU-Kette, CI | keine Signatur (Vertrauensanker = GitHub-Konto); CI unpinned (`pip --upgrade platformio`, Board-JSON per `wget` von master, Actions per Tag) |
| QA-AU-08 | Mittel | Partitionen | crashende, aber gültige App: kein Boot-Counter-Rückfall | bekannt SB-05 |
| QA-AU-09..14 | Niedrig–Mittel | siehe Bericht | Apply ohne Batterie-Floor, jahrloser Versionsvergleich (AU-D10), Stage-Record überlebt Web-Upload, Safeboot-Headroom ohne CI-Gate, BLE-DFU ohne Bonding |
| QA-AU-15 ✔ | Niedrig | `ElegantOTA.cpp:109` | `_server>sendHeader(...)` (Vergleich statt Aufruf), nur im nicht kompilierten Sync-Zweig |

## 3. Firmware-Größe

Berichte: [qa-20261025/hwgating.md](qa-20261025/hwgating.md) (Matrix Env × Feature, Delta-Messungen),
[qa-20261025/sizemap.md](qa-20261025/sizemap.md) (Attribution aller 32 ELFs, Top-25-Dateien je Repräsentant).

### 3.1 Baseline (pio, 32/32 grün)

Safeboot-Limit = factory-Partition 704 K (pio zeigt fälschlich 3,4 MB). RAM % ist statisch, nicht Heap.

| Env | Flash B | Limit B | Flash % | Headroom KB | RAM % |
|---|---:|---:|---:|---:|---:|
| esp32-safeboot | 713 411 | 720 896 | 99.0 | 7 | 14.8 |
| esp32-S3-safeboot | 674 303 | 720 896 | 93.5 | 46 | 13.3 |
| wiscore_rak4631 | 702 116 | 815 104 | 86.1 | 110 | 39.6 |
| t_echo | 675 688 | 815 104 | 82.9 | 136 | 41.7 |
| heltec_t114 | 650 420 | 815 104 | 79.8 | 161 | 41.6 |
| ttgo_tbeam_SX1262 | 2 020 601 | 3 403 776 | 59.4 | 1351 | 7.9 |
| ttgo_tbeam_SX1268 | 2 020 537 | 3 403 776 | 59.4 | 1351 | 7.9 |
| ttgo_tbeam | 2 012 533 | 3 403 776 | 59.1 | 1359 | 7.9 |
| E22_XML-DevKitC | 2 010 021 | 3 403 776 | 59.1 | 1361 | 19.9 |
| E22_1262-DevKitC | 1 990 857 | 3 403 776 | 58.5 | 1380 | 19.5 |
| E22-DevKitC | 1 990 713 | 3 403 776 | 58.5 | 1380 | 19.5 |
| esp32-loraprs-e22 | 1 990 505 | 3 403 776 | 58.5 | 1380 | 19.5 |
| heltec_wifi_lora_32_V2 | 1 982 313 | 3 403 776 | 58.2 | 1388 | 31.7 |
| esp32-loraprs-ra01 | 1 979 897 | 3 403 776 | 58.2 | 1391 | 19.5 |
| ttgo-lora32-v21 | 1 979 733 | 3 403 776 | 58.2 | 1391 | 31.7 |
| LilyGo_T_Connect_Pro | 1 974 369 | 3 403 776 | 58.0 | 1396 | 39.7 |
| T-ETH-ELITE_1262 | 1 937 517 | 3 403 776 | 56.9 | 1432 | 39.6 |
| vision-master-e290 | 1 935 505 | 3 403 776 | 56.9 | 1434 | 40.6 |
| ttgo_tbeam_supreme | 1 931 853 | 3 403 776 | 56.8 | 1437 | 39.4 |
| heltec_wireless_tracker | 1 920 001 | 3 403 776 | 56.4 | 1449 | 39.5 |
| LilyGo_T-Beam-1W | 1 918 085 | 3 403 776 | 56.4 | 1451 | 24.3 |
| LilyGo_T3_S3_V1_3 | 1 913 989 | 3 403 776 | 56.2 | 1455 | 39.5 |
| E22_1262_S3-DevKitC-1-N16R8 | 1 913 009 | 3 403 776 | 56.2 | 1456 | 39.5 |
| E22_1268_S3-DevKitC-1-N16R8 | 1 912 933 | 3 403 776 | 56.2 | 1456 | 39.5 |
| heltec_wifi_lora_32_V3 | 1 896 117 | 3 403 776 | 55.7 | 1472 | 39.3 |
| heltec_wifi_lora_32_V4 | 1 895 669 | 3 403 776 | 55.7 | 1473 | 39.4 |
| heltec_wireless_stick | 1 893 049 | 3 403 776 | 55.6 | 1475 | 39.3 |
| vision-master-e213 | 1 888 445 | 3 403 776 | 55.5 | 1480 | 40.0 |
| wireless-paper | 1 863 225 | 3 403 776 | 54.7 | 1504 | 39.9 |
| t_deck | 2 518 997 | 12 582 912 | 20.0 | 9828 | 45.4 |
| t_deck_plus | 2 518 805 | 12 582 912 | 20.0 | 9828 | 45.4 |
| t_deck_pro | 2 302 321 | 12 582 912 | 18.3 | 10040 | 47.0 |

nRF52: Linker-Grenze `0x26000 + 0xC7000`, darüber LittleFS (28 KB) und Bootloader. Überlauf bricht den Link hart
ab; das Limit lässt sich nicht anheben.

### 3.2 Wohin der Flash geht (Attribution)

- **V3 (S3):** SDK-Blobs 308 KB, src-Kern 303, **web_functions 205 (11 %)**, mbedtls 139, newlib 123, lwip 93,
  NimBLE 73, eh_frame 70.
- **heltec_t114:** src-Kern 245 (38 %), **web_functions 167,5 (26 %)**, nrf52 45, CryptoCell 31, TinyUSB 22,
  W5100S 9,7.
- **t_deck_plus:** LVGL 245, web 207, Audio ≈147 (davon AAC ≈55–60).
- Größte Einzeldateien: `web_functions.cpp` 135 KB (87 KB Strings), `command_functions.cpp` 73 KB,
  `web_rm_page.cpp` 46 KB (45 KB Strings), newlib `vfprintf` 48 KB, `mdns.c` 21 KB.

### 3.3 AU-Staging als zweite Grenze (ESP32)

**QA-SIZE-AU01 (neu, Hoch für die Classic-Boards).** `fwStageLayout()` (`fw_update.h:252`) legt das komprimierte
Image ans Ende von ota_0 (3324 K, alle 27 ESP32-Apps außer T-Deck) und verlangt `laufendes Image ≤ off` und
`neues Image ≤ off` mit `off = (Slot − zLen) & ~0xFFFF` (`FW_STAGE_ALIGN` 64 K). Die echte Wachstumsgrenze ist
also nicht der Flash-Headroom (41–45 %), sondern App + `.autoupdate`. Gerechnet mit den echten Asset-Größen von
`v4.40a.10.25` (GitHub-API):

| Env | .bin B | .autoupdate B | Reserve heute KB | max. App-Wachstum KB |
|---|---:|---:|---:|---:|
| ttgo_tbeam_SX1262 / _SX1268 | 2 027 600 | 1 269 957 | 100 | **61** |
| ttgo_tbeam | 2 019 520 | 1 264 259 | 116 | **71** |
| E22_XML-DevKitC | 2 017 024 | 1 264 988 | 118 | **73** |
| E22-DevKitC, E22_1262, esp32-loraprs-e22 | 1 997 712 | 1 255 916 | 145 | 89 |
| heltec_wifi_lora_32_V2, loraprs-ra01, ttgo-lora32-v21 | 1 989 328 | 1 249 927 | 157–164 | 97–101 |
| t_connect_pro | 1 975 104 | 1 237 721 | 183 | 113 |
| S3-Boards (T-ETH, e290, supreme, tracker, 1W, E22_S3, T3_S3) | 1 913 664–1 938 240 | 1 208 189–1 219 284 | 239–274 | 147–168 |
| heltec V3 / V4 / stick (DK5EN-98) | 1 896 832 | 1 198 262 | 300 | 184 |
| vision-master-e213, wireless-paper | 1 863 952–1 889 168 | 1 172 698–1 191 529 | 315–356 | 193–218 |

„Max. App-Wachstum" = Reserve / (1 + zLen/bin): jedes App-Byte kostet ≈1,63 Byte Slot. Wird die Grenze
überschritten, verweigert das AU mit `FW_REF_OVERLAP`, und das Board ist nur noch per USB oder Safeboot-Web
aktualisierbar. Es gibt kein CI-Gate dafür (QA-AU-12). Die T-Beam-Familie hat also noch rund 60 KB, das sind wenige
Feature-Wellen. **Empfehlung:** ein CI-/Gate-Check `fwStageLayout(slot, bin, z, bin)` je Env in
`tools/regression.sh` oder `meshcom-ci.yml` und QA-SIZE-01/02 vorziehen. Auf E22-DevKitC gemessen bringen sie
−139 KB App, also ≈ +85 KB Wachstumsreserve (§3.5).

### 3.4 Hebel, priorisiert (Ersparnis × Risiko)

| # | ID | Hebel | Envs | Ersparnis | Risiko | Konf. |
|---|---|---|---|---|---|---|
| 1 | QA-SIZE-05 | `HAS_ETHERNET` auch für nRF52; T114/t_echo erben `-D BOARD_RAK4630` (`platformio.ini:124`) und linken W5100S + komplette Web-GUI, die nur über Ethernet erreichbar ist | heltec_t114, t_echo | 192 / 188 KB (mit extudp/udp_frame 204 / 199) | mittel/hoch: viele Aufrufstellen, Bastel-ETH an T114 (`heltec_t114/configuration.h:49`) | Modell ±10 KB |
| 2 | QA-SIZE-M01 ✔ | Safeboot `ota.html` als gzip (36 639 → 11 682 B); Header-Zeile in `ElegantOTA.cpp:99` steht auskommentiert bereit, `convert.sh` + `gzip -9n` | 2 Safeboot | 24,9 KB | niedrig | gemessen |
| 3 | QA-SIZE-01 | `build_unflags = -fexceptions`, `-fno-exceptions` (kein `try/catch/throw` in src/lib) | 27 ESP32-Apps | 106–130 KB | niedrig/mittel: `new`-Fehler endet wie heute in `abort()`; ttgo_tbeam* (eigenes `build_unflags`) und Envs ohne `${esp32.build_flags}` extra | V3/T-Deck gemessen, Rest kalibriert |
| 4 | QA-SIZE-02 | `CORE_DEBUG_LEVEL=0` | 26 ESP32-Apps | 34–46 KB | niedrig: Arduino-`[E]`-Zeilen fehlen in Feldlogs | gemessen V3 |
| 5 | QA-SIZE-07a/b | LVGL: ungenutzte Widgets aus (`lv_conf.h`); TFT_eSPI nur `LOAD_GLCD` | t_deck, t_deck_plus | 38,1 + 18,6 KB | niedrig | gemessen |
| 6 | QA-SIZE-14/15/13 | TinyUSB nur CDC (`CFG_TUD_*=0`); `lib_archive` auf yes; `SFE_UBLOX_GNSS myGPS` nur unter `ENABLE_RAK_GPS` | 3 nRF52 | 11 + 4,3 + 2,2 KB | niedrig (lib_archive: Bench, ISR-Overrides) | gemessen |
| 7 | QA-SIZE-08 | `${esp32.build_flags}` (NimBLE-Rollen) in 4 Varianten ergänzen | vision-master-e213/e290, wireless-paper, t_deck_pro | 8,9 KB + 0,9 KB RAM | niedrig/mittel (MAX_CONNECTIONS 3→1) | gemessen |
| 8 | Web-gzip | CSS+JS als statische gzip-Ressourcen (`/mc.css`, `/rm.js`) | 27 ESP32 + wiscore | 34 KB (Blob-Maximum 65) | mittel, Aufwand 2–4 Tage (Chunk-Schreibpfad, `webgui_rm_test.js`) | gemessen (gzip -9) |
| 9 | QA-SIZE-07c/M08 | Audio: AAC/FLAC-Decoder aus (vendored Lib, `#ifdef`) | t_deck, t_deck_plus | 55–77 KB | mittel (Sounds von SD) | Schätzung |
| 10 | QA-SIZE-06/09/12 | Sensoren (`ENABLE_<X>_DISABLED`), GPS, U8g2 je Variante | je nach Board | 38 / 24,5 / 19,8 KB | mittel: Feature-Verlust DIY; GPS- und U8g2-Gating haben Compile-Blocker (`loop_functions.cpp:3259`, `:1361`) | gemessen V3 |
| 11 | QA-SIZE-11 | mDNS abschaltbar (`MC_NO_MDNS`) | 27 ESP32 + Safeboot | 23,8 KB | mittel (`<call>.local` weg) | Map |
| 12 | QA-SIZE-03 | `-fstack-protector` aus | 27 ESP32 | 18–24 KB | **Sicherheitsabwägung**: Canaries schützen die char[]-Parser; nicht empfohlen | gemessen |
| 13 | QA-SIZE-04 | AU/TLS als Build-Option | 27 ESP32 | 161 KB | hoch (Produktfunktion) | gemessen |
| 14 | Rest | KISS-TCP 10,7, Netconsole 4,9, MC_DIAG 3,8, nRF libc ≈10–15 (M04), doppelte Settings-Tabellen 10 KB (M07) | verschieden | 2–15 KB | niedrig–mittel | gemischt |

Nicht empfohlen: LTO (nRF52-GCC 7.2 scheitert im Assembler, ESP32 `-fno-lto` im Framework), `ARDUINOJSON_USE_DOUBLE=0`
(Positionspräzision), `RADIOLIB_CHECK_PARAMS=0`. Für Safeboot wirken QA-SIZE-01/02/03 nicht (IDF 5 verwirft
`.eh_frame`, `-Oz`/LTO schon aktiv).

**Nach Engpass:**

- Safeboot classic: QA-SIZE-M01 zuerst (7 → 32 KB Luft), danach mDNS.
- t_echo / heltec_t114: QA-SIZE-05 allein hebt den Headroom auf ≈40 / 44 %.
- wiscore_rak4631 (hat ETH, also kein SIZE-05): #6 + Web-gzip + M04 + MC_DIAG ≈60 KB.
- ESP32-Apps: #3 + #4 = 140–175 KB je Env (7–9 %), das wirkt doppelt auf das AU-Staging.

### 3.5 Verifikation am E22-DevKitC (pio, Classic ESP32)

pio-Build in einer Scratch-Kopie, nur `[env:E22-DevKitC]` geändert: `build_unflags = -fexceptions`,
`-fno-exceptions`, `-D CORE_DEBUG_LEVEL=0`. Gebaut und gelinkt ohne Warnung zu `CORE_DEBUG_LEVEL`.

| Build | Flash B (pio) | Δ |
|---|---:|---:|
| Baseline | 1 990 713 | – |
| QA-SIZE-01 + 02 | 1 848 121 | **−142 592 B (−139 KB, −7,2 %)** |

Die Vorhersage aus der V3-Kalibrierung für Classic (106 + 34–35 KB abzüglich Überlapp) stimmt damit. RAM
−112 B. Nicht geprüft: Laufzeitverhalten (OOM-Pfad `new` → `abort()`, Feldlogs ohne Arduino-`[E]`-Zeilen); dafür
braucht es Bench.

## 4. DRY

Bericht: [qa-20261025/dry.md](qa-20261025/dry.md). Flash spart nur ein Duplikat im selben Binary;
ESP32↔nRF52-Zwillinge sparen 0 Byte und lohnen nur wegen Drift.

| ID | Nutzen | Stelle | Kern | Ersparnis | Risiko |
|---|---|---|---|---|---|
| QA-DRY-01 ✔ | Hoch | `web_setup.cpp:24-922` (setParam 9 906 B), `:924-1311` (getParam 2 746 B) | 72 gleich gebaute Rungen → Tabelle `{name, cmd, Typ, Getter}` + eine Engine | 4–7 KB je ESP32-Web-Env | mittel (Web-API-Strings, Float-Formate; `producer_match_lint.py` anpassen) |
| QA-DRY-02 | Mittel | `command_functions.cpp` setown*/setssid/hamnet, 8× Arg-Copy+LF-Strip | Helper `cmdSetNetStr`, `cmdArgCopyTrim`; behebt QA-CMD-01 nebenbei | 0,8–1,5 KB | niedrig |
| QA-DRY-05 | Mittel | `extudp_functions.cpp:781-830, 934, 1022` | Sendeblock 4×, Halbier-Log 2× | 0,3–0,5 KB | niedrig (Log-Formate fix) |
| QA-DRY-06 | Mittel | 9× `MyClock → node_date_*`, 2× Set-Clock-Block | `syncNodeDateFromClock()`; behebt Schwellen-Drift (2020 vs 2023) | 0,3–0,5 KB | niedrig |
| QA-DRY-07 | Mittel | `hmac_sha256.cpp` neben mbedtls-SHA-256 im ESP32-Image | ESP32 auf mbedtls abbilden | ≈1,3 KB × 29 | mittel (RM-Auth, Bench nötig) |
| QA-DRY-11 | Niedrig | `lora_setchip.cpp:300-461` | **Twin-Drift:** SX126x-Arduino-Zweig (RAK/T114/T-Echo) kennt kein RX-US, kein PL, feste APRS-Frequenz | – | Funkverhalten, Betreiber |
| QA-DRY-03/04 | – | RX-Re-Arm 12×, TX-Start 3× | **Finger weg** außer byte-gleichen Paaren: Produktions-Funkpfad, Reihenfolge absichtlich verschieden | 0,2–0,5 KB | hoch |
| QA-DRY-08..14 | Niedrig | siehe Bericht | Hex/HMAC-Challenge 2×, nbrExclusive 2×, Display-Guards, Twins, GPS-UART-Alias | je <0,2 KB | niedrig |

Schnellgewinn nebenher **QA-SIZE-B01 ✔**: `bmp390.cpp:5` prüft `ENABLE_BMX280` statt `ENABLE_BMP390`
(`bmp390.h:7`); auf T-ETH-ELITE, t_deck, t_deck_plus und t_echo wird `bmp390.cpp` trotz abgeschaltetem BMP390
übersetzt.

## 5. Empfohlene Reihenfolge

Für die Upstream-PRs gilt REVIEW.md: klein und je Thema ein PR.

1. **Gateway-Härtung (Prod):** QA-NET-04 (flush), QA-STAB-02 (Re-Resolve), QA-STAB-01 (async DNS EXTUDP). Klein und
   lokal, Bench an DK5EN-92 vor DK5EN-98.
2. **Web/BLE-Gates:** QA-NET-03/QA-AU-02 (`otaupdate`, `reboot` unter X-MC; ACAO weg von `/config*`),
   QA-NET-01 (Dispatch-Gate bei `bt_code>0`), QA-NET-09, QA-CMD-01. Wire-neutral; die App-Kompatibilität (alte Apps
   ohne Hello) gegen die Flotte prüfen (REVIEW.md Leitsatz 1).
3. **Safeboot:** QA-SIZE-M01 (gzip) zuerst, das schafft Luft für QA-AU-01 (Trockenlauf-Inflate vor dem Erase).
   CI-Verify-Schritt gegen QA-AU-05.
4. **Größe, Flags:** zuerst das AU-Staging-Gate je Env (QA-SIZE-AU01), danach QA-SIZE-01 + 02 als ein
   Build-Config-PR, mit Gate über alle 30 Apps und Bench des OOM-Pfads. Danach QA-SIZE-07a/b, -14 und -08.
5. **Größe, Struktur:** QA-SIZE-05 (nRF52 `HAS_ETHERNET`) nach Betreiberentscheid zur Bastel-ETH an T114.
6. **DRY:** QA-DRY-01 und -02, wenn die Dateien ohnehin angefasst werden.
7. **Protokollebene (langfristig, Upstream-Diskussion):** msg_id-/ACK-Authentisierung (QA-RX-03/04), Rate-Limit
   und Hop-Klemme (QA-RX-05), `{SET}`-Wear (QA-RX-02), Image-Signatur (QA-AU-06).

## 6. Offene Entscheidungen für den Betreiber

- QA-SIZE-05: Gilt die Bastel-ETH an T114/t_echo als unterstütztes Feature?
- QA-SIZE-03: Stack-Canaries behalten? Empfehlung: ja.
- QA-SIZE-06/09/12: Sensoren, GPS, OLED je Variante abschalten? Dafür fehlen Feld-Nutzungsdaten.
- QA-NET-01: Darf eine App ohne Hello noch schreiben, wenn `bt_code` gesetzt ist? Alte Apps im Feld berücksichtigen.
- `MC_WEB_GUARD=1` als Release-Default (`webflash.py` sendet X-MC schon für `upd*`).

## 7. Grenzen

- Kein Hardware-Test, keine Laufzeitmessung (Heap, Stack-HWM, Airtime).
- SIZE-05 und die Flag-Hochrechnung je Env sind ein Modell bzw. am V3 kalibriert; verifiziert ist nur §3.5.
- Nicht geprüft: KISS/TCP-Port 8001 im Detail, Display-/T-Deck-UI, Pages-Web-Flasher, Bootloader-Verhalten.
- Build-Hinweis Cloud: GitHub-`/archive/*.zip` liefert hier 403; `ETHClass2` wurde für den Messbuild per
  `.git#v1.0.0` (gleiche Quelle) bezogen. Der pioarduino-penv braucht `platformio` von PyPI statt des GitHub-ZIPs.

## 8. Umsetzung 2026-10-26

Branch `claude/fork-dev-code-review-xdxqzv`, 17 Arbeitspakete (WP1 bis WP17) als je ein Commit auf Basis von `950fe5cd`.
Alle Änderungen sind in der Cloud entstanden: Host-Tests und Builds, **kein Hardware-Test**. Betreiberentscheidungen
(binden): QA-NET-01 bleibt unverändert (BLE-App ohne PIN-Hello darf schreiben), `MC_WEB_GUARD=1` ist Release-Default,
Stack-Canaries bleiben, Ethernet auf T114/t_echo ist ein Build-Flag (Default aus, RAK4631 an), nRF52-BLE-Settings
maskieren Secrets. Kein Wire-Format- und kein Settings-Layout-Wechsel.

### 8.1 Pakete

| WP | Commit | Status | Inhalt |
|---|---|---|---|
| WP1 | `52a8d5bd` | committed | QA-NET-04, QA-STAB-01/02: UDP-Datagramm wird vollständig geleert (Downlink bleibt nicht hängen), Server-DNS wird neu aufgelöst (Refresh mit Keep-old-Schutz), EXTUDP-DNS asynchron (nur ESP32). |
| WP2 | `6fc54471` | committed | `MC_WEB_GUARD` Default 1; `otaupdate` und `reboot` brauchen `X-MC`; keine `Access-Control-Allow-*`-Header mehr (Secrets); `webid`-Grenzprüfung; Header-Zeile auf 1023 B begrenzt (431); Web-API-Doku und Tests angepasst. |
| WP3 | `538314e4` | committed | QA-CMD-01, QA-DRY-02: `cmdArgCopyTrim()` ersetzt 8 Strip-Stellen (kein Überlauf), Reboot-Check-Helper für `setown*`/`setssid`/`hamnet`. |
| WP4 | `9c52a6d6` | committed | QA-NET-02 (nur nRF52): Settings-Characteristic maskiert `node_passwd`, `node_webpwd`, `node_pwd`, `bt_code` (Sentinel -1); Schreiben mit maskiertem Feld behält das Live-Secret. |
| WP5 | `be599631` | committed | QA-AU-01/09/12: Dry-Run-Inflate prüft die entpackte Länge und den App-Descriptor vor der Übergabe an Safeboot, Batteriegrenze beim Apply, neue Fehlertokens `inflate_chk`/`image` in der Web-UI, `tools/au_stage_check.py` plus CI-Schritt als Staging-Gate. |
| WP6 | `bf8dfd6c` | committed | QA-SIZE-M01, QA-AU-11, QA-AU-15: Safeboot-OTA-Seite gzip (`ota_html_len` 36 639 auf 11 682), `onOTAStart()` verwirft einen alten Stage-Record, Tippfehler `_server->sendHeader`. |
| WP7 | `8fba27c2` | committed | QA-SIZE-01/02/08: `-fno-exceptions`, `CORE_DEBUG_LEVEL=0` in allen ESP32-Envs, NimBLE-Rollen-Flags überall; ein `snprintf`-Format in `command_functions.cpp` (`%.5s`) wegen des Compiler-Verhaltens. Golden neu erzeugt. |
| WP8 | `3bb1d9e4` | committed | QA-SIZE-05: Ethernet als Build-Flag je nRF52-Env (aus auf T114/t_echo, an auf RAK4631), nur TinyUSB-CDC, GPS-Objekt-Guard; `--gateway on` wird ohne Ethernet abgelehnt. |
| WP9 | `ba94f99f` | committed | T-Deck: 18 ungenutzte LVGL-Widgets aus, eigenes `tft_setup.h` (nur genutzte TFT_eSPI-Fonts). Messung im WP: -59,5 KB je t_deck/t_deck_plus. |
| WP10 | `ce7dc8ba` | committed | QA-SIZE-B01 (`bmp390.cpp` prüft `ENABLE_BMP390`), QA-STAB-06/NET-10 (Net-Console: Gesamt-Deadline für den Handshake, accept-Fehlerlog auf 1 pro 30 s begrenzt; nur ESP32). |
| WP11 | `adc8946a` | committed | QA-STAB-03 (NimBLE-Host-Callbacks loggen nicht mehr, Ausgabe in `loop()`; `[BLE];stat` zeigt `hoststack;free`), QA-STAB-04 (Stack-Puffer statt `String` in `decodeAPRSPOS`/`aprsExtractTag`). |
| WP12 | `4f485e34` | committed | QA-RX-02/03/07 und `:rej`-Offset: `{SET}` wird höchstens alle 10 min gespeichert, Echo-Scan nur nach Decode (ohne ACK-Zweig), Sonderrufzeichen als Absender abgelehnt. |
| WP13 | `b372f0a1` | committed | QA-DRY-05/06/09/13: gemeinsame EXTUDP-Sendehelfer, `syncNodeDateFromClock()`, `nbrExclusiveImpl`, `GPS_UART`-Alias (19 von 27 Stellen). |
| WP14 | `d6437a46` | committed | SHA-256/HMAC über mbedtls auf ESP32 (nRF52 unverändert); `glueHmac` bewusst nicht zusammengelegt (Begründung im Code). |
| WP15 | `d45056b1` | committed | QA-DRY-01: tabellengetriebene `setParam`/`getParam` (`wsParams[]`); `producer_match_lint.py` angepasst. |
| WP16 | `365cd3bf` | committed | Statisches CSS/JS als gzip-Assets (`/mc.css`, `/mc.js`, `/mcrm.js`, `/mcau.js`) mit Cache-Header; Generator `tools/gen_web_assets.py`. nRF52: AU-CSS-Regeln gelten jetzt überall. |
| WP17 | `17793944` | committed | QA-AU-05: CI legt das Release als Draft an, prüft Asset-Paare (`tools/verify_release_assets.py`), veröffentlicht erst danach; Re-Run fasst kein veröffentlichtes Release an. |

Kein WP ist gescheitert. Beim Abschluss-Gate trat ein Folgefehler aus WP5 auf (siehe 8.3), der in diesem Commit behoben ist.

### 8.2 Bewusst nicht umgesetzt (Skipped)

- **WP1:** keine Tests für die DNS-Zustandsautomaten (inline `static`, nativ nicht testbar); kein frühes Return für Literal-IPs; synchroner `ip==0`-Pfad in `extDnsResolve` versucht jeden Durchlauf erneut (Nits).
- **WP2:** `X-MC` kommt über den globalen fetch/XHR-Wrapper; ob es im 1023-B-Header-Fenster bei `guard=1` ankommt, ist im Browser ungeprüft.
- **WP3:** kein `setown*`-Snprintf-Helper, kein gemeinsamer Reboot-Helper (Werte unterscheiden sich); Reboot-/Hamnet-Helper nicht host-testbar (nur `cmdArgCopyTrim` getestet).
- **WP4:** Verhalten der App bei `bt_code = -1` nicht prüfbar (App-Quelle außerhalb des Repos); SN1-Notify `WSPWD` im Klartext (außerhalb QA-NET-02).
- **WP5:** kein Heap-Check vor dem Download (Dict und Dekompressor sind getrennt alloziert); kein IDF-5-Compile von `fw_update_net.cpp` (kein App-Env nutzt IDF 5); neue Python-Dateien nicht mit `ruff format` formatiert.
- **WP6:** `ota.h` mit einem Python-`xxd -i`-Shim erzeugt (echtes `xxd` fehlt; bei Neuerzeugung evtl. nur Whitespace-Diff).
- **WP8:** tote Funktionsrümpfe (`webpwd`, `webtimer 0`, `extudpip`, `srvip`, `ntpsync`) nicht mit `#if` umzäunt; `config_json.cpp` nicht gegated (geteilt mit Settings-Store und BLE-Settings); TinyUSB-CDC/UF2 nicht an Hardware geprüft. In der PR-Beschreibung erwähnen: zusätzliches `lib_ignore` für SparkFun u-blox auf T114/t_echo.
- **WP9:** keine Größenmessung gegen gleiche Basis außerhalb des WP; Golden-Gates im WP nicht gelaufen (hier im Gate nachgeholt).
- **WP10:** Handshake-Deadline und accept-Log-Rate-Limit nicht zur Laufzeit geprüft.
- **WP11:** weitere `Serial.printf` in den Callbacks (`bBLEDEBUG`, Disconnect-Reason, PassKey) bleiben; `extudp_functions.cpp` String-Pfade unverändert.
- **WP12:** QA-RX-03 und QA-RX-11 ohne Host-Test (in `OnRxDone` und den `udp_frame`-Zwillingen, nativ nicht testbar).
- **WP13:** 8 GPS-`#if`-Stellen nicht aliasbar (Pins, Baudrate, Schreibweise); Set-Clock-Blöcke nicht zusammengelegt (andere Jahresschwellen); keine nativen Tests für DRY-05/06/13.
- **WP14:** `sha256Abort`/Free auf OTA-Fehlerpfaden nicht angefasst (braucht Bench gegen die gepinnte IDF).
- **WP15:** `mesh`, `rm`, `rmstrict`, `display`, `boardled`, `chargeled`, `aumode`, `autoupdate`, `updchan`, Store-Gruppe, `setvia`, `tempoffset*` bleiben explizit (Tests pinnen Text oder Reihenfolge). **Bekannter Fehler unverändert:** GET `tempoffsetoutdoor` liefert UNKNOWN (doppelter `tempoffsetindoor`-Zweig), braucht Freigabe. `webgui_settings_test.js` im Cloud-WP ohne jsdom nicht gelaufen (im Gate unten grün).
- **WP16:** Serving-Pfad nur kompiliert und host-getestet, nicht im Browser; `webgui_tick_test.js`/`webgui_badge_test.js` (Live-Node-Tests) nicht gelaufen.
- **WP17:** `ncipollo/release-action`-Eingaben (`artifactErrorsFailBuild`, `draft` beim Update) und der `gh api`-Draft-Download aus dem Gedächtnis; der erste `ci-proof-*`-Lauf muss das bestätigen. `actionlint` nicht verfügbar.
- Nicht aus dem Report aufgegriffen: QA-NET-01 (Betreiber), Stack-Canaries (Betreiber), QA-SIZE-06/09/12 (Sensoren/GPS/OLED, keine Feldnutzungsdaten).

### 8.3 Gate

`tools/regression.sh --stage 1,2` auf `17793944` (Stand nach WP17), `MC_AU_DEV_CHANNEL=1`:

- Stage 1: 83 Unity-Envs, 3028 Fälle, 3021 ok, 2 failed, 1 skipped. Golden-Selftest 48 Kommandos grün; Board-Links nRF52 grün (RAK4631 78,7 %, T114 49,8 %, t_echo 52,9 %).
- Stage 2: pytest 1422 passed; alle node-/nbrlog-/Self-Test-Suiten und `ruff` grün (0 Fehler); `topo_shadow raw window` übersprungen (rpizero nicht erreichbar).
- Bekannte Cloud-Fehler (gcc 13, Baseline): `native` `test_pos_filter::test_kadenz_unabhaengig`; `native_udp_frame_twin` kompiliert nicht ("taking address of temporary array").
- **Neuer Fehler, behoben:** `native_command_setters::test_stage_flash_goes_through_one_guarded_helper`. Der Source-Pin erlaubt `stageFlash(STAGE_` nur in `dlFeed`/`stagePrepare`/`stageFinish`; WP5 ruft `stageFlash(STAGE_READ, ...)` aus dem neuen Dry-Run-Reader `DryRd`. Die WP-Berichte hielten den Fehler für Baseline, er stammt aber aus WP5. Fix: der Test erlaubt zusätzlich den Bereich `struct DryRd` bis `struct DryWr` (nur lesend, derselbe geschützte Helper). `pio test -e native_command_setters` danach grün (50 Fälle).
- Nicht geändert: `safeboot.bin` und `safeboot-s3.bin` im Repo-Root (Safeboot-Builds überschreiben sie, nach dem Build zurückgesetzt). Die Images für die Pages/den Web-Flasher entstehen beim Release aus CI.

### 8.4 Größen (alle 32 CI-Envs gebaut, `MC_AU_DEV_CHANNEL=1`)

Alle 32 Envs bauen. `LilyGo_T_Connect_Pro` und `T-ETH-ELITE_1262` scheitern in der Cloud am 403 auf `ETHClass2/archive/v1.0.0.zip`
und wurden in einer Kopie mit `ETHClass2.git#v1.0.0` gebaut (gleiche Quelle). Baseline = §3.1 (gleiche Cloud, gleiche Messmethode wie dort).
Sortiert nach Flash-Auslastung. Safeboot-Limit 720 896 B (factory-Partition 704 K).

| Env | Baseline B | Neu B | Delta B | Limit B | Flash % | Headroom KB | RAM % (Basis) |
|---|---:|---:|---:|---:|---:|---:|---:|
| esp32-safeboot | 713 411 | 688 487 | -24 924 | 720 896 | 95.5 | 31 | 14.8 (14.8) |
| esp32-S3-safeboot | 674 303 | 649 515 | -24 788 | 720 896 | 90.1 | 69 | 13.3 (13.3) |
| wiscore_rak4631 | 702 116 | 641 536 | -60 580 | 815 104 | 78.7 | 169 | 38.6 (39.6) |
| ttgo_tbeam_SX1262 | 2 020 601 | 1 807 189 | -213 412 | 3 403 776 | 53.1 | 1559 | 8.0 (7.9) |
| ttgo_tbeam_SX1268 | 2 020 537 | 1 807 145 | -213 392 | 3 403 776 | 53.1 | 1559 | 8.0 (7.9) |
| E22_XML-DevKitC | 2 010 021 | 1 802 997 | -207 024 | 3 403 776 | 53.0 | 1563 | 20.1 (19.9) |
| t_echo | 675 688 | 431 372 | -244 316 | 815 104 | 52.9 | 374 | 38.4 (41.7) |
| ttgo_tbeam | 2 012 533 | 1 799 469 | -213 064 | 3 403 776 | 52.9 | 1566 | 8.0 (7.9) |
| E22_1262-DevKitC | 1 990 857 | 1 791 161 | -199 696 | 3 403 776 | 52.6 | 1574 | 19.7 (19.5) |
| E22-DevKitC | 1 990 713 | 1 791 073 | -199 640 | 3 403 776 | 52.6 | 1574 | 19.7 (19.5) |
| esp32-loraprs-e22 | 1 990 505 | 1 790 825 | -199 680 | 3 403 776 | 52.6 | 1575 | 19.7 (19.5) |
| heltec_wifi_lora_32_V2 | 1 982 313 | 1 782 733 | -199 580 | 3 403 776 | 52.4 | 1583 | 32.0 (31.7) |
| esp32-loraprs-ra01 | 1 979 897 | 1 781 361 | -198 536 | 3 403 776 | 52.3 | 1584 | 19.7 (19.5) |
| ttgo-lora32-v21 | 1 979 733 | 1 780 793 | -198 940 | 3 403 776 | 52.3 | 1584 | 32.0 (31.7) |
| LilyGo_T_Connect_Pro | 1 974 369 | 1 760 833 | -213 536 | 3 403 776 | 51.7 | 1604 | 40.0 (39.7) |
| T-ETH-ELITE_1262 | 1 937 517 | 1 729 425 | -208 092 | 3 403 776 | 50.8 | 1635 | 39.8 (39.6) |
| vision-master-e290 | 1 935 505 | 1 717 581 | -217 924 | 3 403 776 | 50.5 | 1646 | 40.9 (40.6) |
| ttgo_tbeam_supreme | 1 931 853 | 1 717 169 | -214 684 | 3 403 776 | 50.4 | 1647 | 39.7 (39.4) |
| LilyGo_T-Beam-1W | 1 918 085 | 1 713 757 | -204 328 | 3 403 776 | 50.3 | 1650 | 24.5 (24.3) |
| heltec_wireless_tracker | 1 920 001 | 1 713 649 | -206 352 | 3 403 776 | 50.3 | 1650 | 39.8 (39.5) |
| E22_1262_S3-DevKitC-1-N16R8 | 1 913 009 | 1 709 917 | -203 092 | 3 403 776 | 50.2 | 1654 | 39.9 (39.5) |
| E22_1268_S3-DevKitC-1-N16R8 | 1 912 933 | 1 709 889 | -203 044 | 3 403 776 | 50.2 | 1654 | 39.9 (39.5) |
| LilyGo_T3_S3_V1_3 | 1 913 989 | 1 709 577 | -204 412 | 3 403 776 | 50.2 | 1654 | 39.9 (39.5) |
| heltec_t114 | 650 420 | 405 900 | -244 520 | 815 104 | 49.8 | 399 | 38.2 (41.6) |
| heltec_wifi_lora_32_V3 | 1 896 117 | 1 694 177 | -201 940 | 3 403 776 | 49.8 | 1669 | 39.6 (39.3) |
| heltec_wifi_lora_32_V4 | 1 895 669 | 1 693 001 | -202 668 | 3 403 776 | 49.7 | 1670 | 39.7 (39.4) |
| heltec_wireless_stick | 1 893 049 | 1 691 181 | -201 868 | 3 403 776 | 49.7 | 1672 | 39.6 (39.3) |
| vision-master-e213 | 1 888 445 | 1 678 537 | -209 908 | 3 403 776 | 49.3 | 1684 | 40.3 (40.0) |
| wireless-paper | 1 863 225 | 1 656 101 | -207 124 | 3 403 776 | 48.7 | 1706 | 40.2 (39.9) |
| t_deck | 2 518 997 | 2 224 585 | -294 412 | 12 582 912 | 17.7 | 10115 | 45.5 (45.4) |
| t_deck_plus | 2 518 805 | 2 224 321 | -294 484 | 12 582 912 | 17.7 | 10115 | 45.5 (45.4) |
| t_deck_pro | 2 302 321 | 2 120 473 | -181 848 | 12 582 912 | 16.9 | 10217 | 47.3 (47.0) |

Highlights:

- **Safeboot:** `esp32-safeboot` 713 411 auf 688 487 B (-24 924), Headroom von 7 KB auf 31 KB; `esp32-S3-safeboot` -24 788, Headroom 69 KB (WP6).
- **nRF52:** `heltec_t114` -244 520 B (49,8 %), `t_echo` -244 316 B (52,9 %), `wiscore_rak4631` -60 580 B (78,7 %), jeweils Ethernet-Flag, Web-Gzip und TinyUSB-Trim.
- **ESP32-Familie:** -199 KB bis -214 KB (`-fno-exceptions`, `CORE_DEBUG_LEVEL=0`, gzip-Web-Assets); T-Deck -294 KB (zusätzlich LVGL/Fonts), `t_deck_pro` -182 KB.
- **RAM (statisch):** ESP32 +0,1 bis +0,3 Prozentpunkte (`wsParams[]`, Puffer, Dry-Run-Strukturen), nRF52 -1 bis -3,5 Prozentpunkte.

Deltas der einzelnen WPs (A/B im jeweiligen WP, Flash Bytes): WP1 +980 (V3) / +984 (E22) / +16 (RAK); WP2 -4 (V3) / -712 (RAK); WP3 -784 (V3) / -1 232 (RAK);
WP4 -864 (RAK) / -880 (T114) / -872 (t_echo); WP5 +592 (V3) / +620 (E22); WP6 -24 924 / -24 788 (Safeboot); WP7 -142 KB (E22) bis -175 KB (t_deck);
WP8 T114 -244 256 / t_echo -244 016 / RAK -11 872; WP9 -60,9 KB (t_deck, t_deck_plus); WP10 +120 (V3) / -36 (t_echo) / +84 (t_deck);
WP11 -228 (V3) / -44 (E22) / -608 (RAK); WP12 +172 (V3) / +232 (RAK); WP13 -676 (V3); WP14 -1 112 (V3) / -1 176 (E22) / 0 (RAK);
WP15 -1 376 (V3) / -212 (RAK); WP16 -54 664 (V3) / -47 696 (RAK) / -54,7 KB (E22).

### 8.5 AU-Staging-Gate (QA-SIZE-AU01)

`tools/make_autoupdate.py --check` und `tools/au_stage_check.py` auf den frischen Images aller 27 ESP32-App-Envs: **27 Envs, 0 abgelehnt, 0 unter der Warnschwelle**
(alle `.autoupdate` re-inflate byte-genau). Die T-Beam-Familie hatte vorher nur ≈61 KB Reserve bis zur Ablehnung des Stagings; jetzt:
`ttgo_tbeam_SX1262`/`SX1268` Reserve 371 KB (max. Wachstum 212 KB), `ttgo_tbeam` 384 KB, `E22_XML-DevKitC` 377 KB (216 KB), `E22-DevKitC` 396 KB (228 KB),
`heltec_wifi_lora_32_V3` 554 KB (329 KB). T-Deck-Familie 8,6 MB Reserve. Die Gate-Ausgabe ist die Kennzahl für künftige Image-Zuwächse.

### 8.6 Nicht gebencht: Bench-Liste für DK5EN-92 (ESP32, Heltec V3) und DK5EN-90 (nRF52, RAK4631)

Vorher `identity_guard.py`, Testverkehr nur an Gruppe `TEST` oder direkt an eigene Knoten, TX höchstens 2 dBm, DK5EN-98 nicht anfassen.

DK5EN-92 (ESP32):
1. **WP1:** UDP-Datagramm ≥255 B an den Gateway-Port (eigene Testinstanz, nicht DK5EN-98): Downlink GATE/BEAT/CONF läuft weiter. Server-DNS-Wechsel: Refresh übernimmt neue Adresse, fehlgeschlagene Auflösung behält die alte. EXTUDP mit Hostnamen: Loop blockiert nicht.
2. **WP2:** Web-GUI im Browser mit `guard=1`: alle Seiten laden, `otaupdate`, `reboot`, `upd*` mit und ohne `X-MC`, Header-Zeile >1023 B ergibt 431; prüfen, dass `X-MC` im Header-Fenster liegt; kein CORS-Header.
3. **WP5:** Auto-Update End-to-End über die Web-GUI: Dry-Run bis zum Handover, Fehlertokens `inflate_chk`/`image` mit manipuliertem `.autoupdate`, Verhalten bei niedriger Batteriespannung; Heap-Spitze beim Dry-Run (Dict plus Dekompressor getrennt).
4. **WP6:** Safeboot mit gzip-OTA-Seite (`/ota`) im Browser, Upload; ein alter Stage-Record wird bei `/ota/start` verworfen (Reinstall-Guard `last` bleibt). Beide Safeboot-Images auf einem Board (Classic und S3) booten.
5. **WP7:** Stabilität und BLE ohne Exceptions und mit `CORE_DEBUG_LEVEL=0` (Soak, BLE-Pairing, Heap-Idle gegen 68 728 B aus 10.25); Absturz-Pfade, die vorher eine Exception fingen, im Log suchen.
6. **WP9:** (T-Deck, falls verfügbar) Display und Menüs ohne die entfernten LVGL-Widgets und Fonts; kein fehlendes Glyph.
7. **WP10:** Net-Console (Port 2323): Login-Handshake, langsamer Client (Deadline), accept-Fehlerlog-Rate-Limit.
8. **WP11:** BLE connect/disconnect/Reconnect: Log "BLE Connected with"/"Client connected" erscheint in `loop()`; `[BLE];stat` zeigt `hoststack;free`; APRS-POS-Decode und Gruppenfilter mit Testframes.
9. **WP12:** `{SET}` Hop-Änderung A-B-A: Flash-Schreibrate (höchstens alle 10 min); ACK-Echo-Verhalten ohne 0x41-Zweig; `:rej`-Offset; Sonderrufzeichen als Absender.
10. **WP13:** EXTUDP-Sendepfad (Fehlerfall, Reset), Uhr-Synchronisation (NTP, GPS), `--nbrexcl`-Ausgabe, GPS-UART auf den Boards mit GPS.
11. **WP14:** HMAC/SHA-256 mbedtls-Zweig: RM-Tag-Round-Trip mit bekanntem Schlüssel (alt gegen neu) und OTA-DOWNLOAD-Digest-Match in `stageFinish`.
12. **WP15:** Web-GUI Einstellungen: alle `setParam`/`getParam`-Parameter (Set, Read-back, Float-Nachkommastellen); GET `tempoffsetoutdoor` bleibt UNKNOWN (bekannt).
13. **WP16:** Web-GUI im echten Browser: `/mc.css`, `/mc.js`, `/mcrm.js`, `/mcau.js` laden gzip, Cache-Header, Remote-Seite mit 4 bis 5 zusätzlichen Verbindungen gegen den Single-Thread-Server (Timeouts?); `webgui_tick_test.js`, `webgui_badge_test.js` gegen den Node.
14. **WP17:** erster `ci-proof-*`-Lauf: Draft, `Verify uploaded assets`, kein Publish bei `ci-proof`; danach Draft und Tag löschen. Ein `v*`-Tag veröffentlicht erst nach der Prüfung als Latest.

DK5EN-90 (nRF52, RAK4631):
1. **WP4:** BLE-Settings-Characteristic lesen: `node_passwd`, `node_webpwd`, `node_pwd` leer, `bt_code` -1 bei gesetzter PIN; Schreiben des gelesenen Images lässt die Secrets unverändert, neues Secret wird übernommen. Verträglichkeit der Apps mit `bt_code` -1 (App-Eigner fragen).
2. **WP8:** RAK4631 mit Ethernet (W5100S): Netz, Net-Console, `--gateway on`; TinyUSB-CDC-Enumeration und Serial; UF2-Bootloader; GPS. Auf T114/t_echo (falls verfügbar): `--gateway on` wird abgelehnt, `--gateway off` funktioniert, Boot und BLE ohne Ethernet-Code.
3. **WP12/13/14:** nRF52-Gegenstücke (`udp_frame`, Uhr, `nbrExclusive`) auf Regression prüfen, HMAC unverändert (kein mbedtls).
4. **WP16:** nRF52-Web-GUI: AU-CSS-Regeln gelten jetzt überall (disabled Buttons gedimmt).
