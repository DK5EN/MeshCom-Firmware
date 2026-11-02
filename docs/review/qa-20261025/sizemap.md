# Größen-Attribution und Hebel (alle 32 CI-Envs, fork-dev f0a81c8)

**Methode (alles gemessen am ELF, außer ausdrücklich „Schätzung“).** ESP32: `nm -S -l` plus DWARF-Aranges (Funktion → Übersetzungseinheit), Literal-Pool-Verfolgung per `objdump -d` (`l32r`-Ziele ordnen anonyme `.rodata.str`-Strings der lesenden Datei zu), Zeigertabellen in `.rodata/.data`; Summe je Env = exakt PIO-Flashwert (z. B. V3 1 896 117 B), Rest „unzugeordnet“ 10–15 KB (unsymbolisierte `.dram0.data`). nRF52: **Map-Files existieren** (`size/<env>/firmware.map`, Kollege: „keins“), die ELFs haben dort keine DWARF; Summe Map 653 245 B gegen 650 420 B (+0,4 % Mehrfachzählung gemergter Strings). Safeboot: `size/copy/.pio/build/esp32-*safeboot/safeboot*.{elf,map}`. Skripte: `scratchpad/sizemap/` (`attr3.py`, `mapparse.py`, `reach_nrf.py`, `flags.py`, `webstr.py`). Ganze Top-25-Listen: `attribution_compact.md` im selben Ordner.

## 1. Baseline (32 Envs, nach Flash-% sortiert)

Limit ESP32-Apps 3 403 776 B (Slot 3324 K), T-Deck 12 582 912 B, nRF52 815 104 B, **Safeboot 720 896 B (factory 704 K; das PIO-Limit 3,4 MB ist falsch)**. Fett: unter 25 % Headroom.

| Env | Flash B | Limit B | Flash % | Headroom KB (%) | RAM % |
|---|---:|---:|---:|---:|---:|
| **esp32-safeboot** (Safeboot) | 713411 | 720896 | 99.0 | 7.3 (1.0) | 14.8 |
| **esp32-S3-safeboot** (Safeboot) | 674303 | 720896 | 93.5 | 45.5 (6.5) | 13.3 |
| **wiscore_rak4631** | 702116 | 815104 | 86.1 | 110.3 (13.9) | 39.6 |
| **t_echo** | 675688 | 815104 | 82.9 | 136.1 (17.1) | 41.7 |
| **heltec_t114** | 650420 | 815104 | 79.8 | 160.8 (20.2) | 41.6 |
| ttgo_tbeam_SX1262 | 2020601 | 3403776 | 59.4 | 1350.8 (40.6) | 7.9 |
| ttgo_tbeam_SX1268 | 2020537 | 3403776 | 59.4 | 1350.8 (40.6) | 7.9 |
| ttgo_tbeam | 2012533 | 3403776 | 59.1 | 1358.6 (40.9) | 7.9 |
| E22_XML-DevKitC | 2010021 | 3403776 | 59.1 | 1361.1 (40.9) | 19.9 |
| E22_1262-DevKitC | 1990857 | 3403776 | 58.5 | 1379.8 (41.5) | 19.5 |
| E22-DevKitC | 1990713 | 3403776 | 58.5 | 1379.9 (41.5) | 19.5 |
| esp32-loraprs-e22 | 1990505 | 3403776 | 58.5 | 1380.1 (41.5) | 19.5 |
| heltec_wifi_lora_32_V2 | 1982313 | 3403776 | 58.2 | 1388.1 (41.8) | 31.7 |
| esp32-loraprs-ra01 | 1979897 | 3403776 | 58.2 | 1390.5 (41.8) | 19.5 |
| ttgo-lora32-v21 | 1979733 | 3403776 | 58.2 | 1390.7 (41.8) | 31.7 |
| LilyGo_T_Connect_Pro | 1974369 | 3403776 | 58.0 | 1395.9 (42.0) | 39.7 |
| T-ETH-ELITE_1262 | 1937517 | 3403776 | 56.9 | 1431.9 (43.1) | 39.6 |
| vision-master-e290 | 1935505 | 3403776 | 56.9 | 1433.9 (43.1) | 40.6 |
| ttgo_tbeam_supreme | 1931853 | 3403776 | 56.8 | 1437.4 (43.2) | 39.4 |
| heltec_wireless_tracker | 1920001 | 3403776 | 56.4 | 1449.0 (43.6) | 39.5 |
| LilyGo_T-Beam-1W | 1918085 | 3403776 | 56.4 | 1450.9 (43.6) | 24.3 |
| LilyGo_T3_S3_V1_3 | 1913989 | 3403776 | 56.2 | 1454.9 (43.8) | 39.5 |
| E22_1262_S3-DevKitC-1-N16R8 | 1913009 | 3403776 | 56.2 | 1455.8 (43.8) | 39.5 |
| E22_1268_S3-DevKitC-1-N16R8 | 1912933 | 3403776 | 56.2 | 1455.9 (43.8) | 39.5 |
| heltec_wifi_lora_32_V3 | 1896117 | 3403776 | 55.7 | 1472.3 (44.3) | 39.3 |
| heltec_wifi_lora_32_V4 | 1895669 | 3403776 | 55.7 | 1472.8 (44.3) | 39.4 |
| heltec_wireless_stick | 1893049 | 3403776 | 55.6 | 1475.3 (44.4) | 39.3 |
| vision-master-e213 | 1888445 | 3403776 | 55.5 | 1479.8 (44.5) | 40.0 |
| wireless-paper | 1863225 | 3403776 | 54.7 | 1504.4 (45.3) | 39.9 |
| t_deck | 2518997 | 12582912 | 20.0 | 9828.0 (80.0) | 45.4 |
| t_deck_plus | 2518805 | 12582912 | 20.0 | 9828.2 (80.0) | 45.4 |
| t_deck_pro | 2302321 | 12582912 | 18.3 | 10039.6 (81.7) | 47.0 |
**nRF52 bei Wachstum.** Linker-Script `nrf52840_s140_v6.ld`: `FLASH ORIGIN 0x26000, LENGTH 0xED000−0x26000 = 815 104 B`. Darunter SoftDevice S140 6.1.1 (0x1000–0x26000), darüber `InternalFS`/LittleFS bei `0xED000` (7×4 KB = 28 KB, hält Settings, `InternalFileSystem.cpp:29`) und der Adafruit-Bootloader ab `0xF4000` (Settings-Seite `0xFF000`). Überlauf bricht den Link hart ab („region `FLASH` overflowed“), es gibt keinen stillen Einbruch in LittleFS. Das Limit lässt sich nicht anheben, ohne Settings-FS und Bootloader zu überschreiben; Headroom wiscore 110 KB, t_echo 136 KB, T114 161 KB.

**Safeboot.** Classic nur 7,3 KB (1,0 %), S3 45,5 KB. Beide sind LTO-gebaut (`ltrans`), Quelle nicht mehr dateiweise trennbar. Anteile (Map): classic `libnet80211` 136 K, App+AsyncWS+ElegantOTA (LTO) 109 K, `liblwip` 95 K, `libpp` 64 K, `libphy` 46 K, `libwpa_supplicant` 44 K, `libc_nano` 31 K, `mdns` 23,5 K, `bootloader_support` 18,6 K. Der LTO-Block enthält `ota.html` als **36 639 B Rohtext** (`ota.h`).


## 2. Attribution (Top-Buckets; Top-25-Dateien je Env in Anhang A)

Anonyme Strings sind zugeordnet (Literal-Pool-Trace), nicht geschätzt: V3 288 KB (15,2 % des Images) Strings mit Besitzer, 4,8 KB ohne Referenz, 9 KB Binärreste. „SDK-Blob“ = vorkompilierte Funk-/BT-Bibliotheken ohne DWARF.

| Env | Größte Buckets (KB, % Image) |
|---|---|
| **heltec_wifi_lora_32_V3** (S3) | SDK-Blob 308 (16,7 %), src-Kern 303 (16,3 %), **src/web_functions 205,5 (11,1 %)**, mbedtls 139 (7,5 %), newlib 123 (6,6 %), lwip 93, NimBLE 73, **eh_frame 70 (3,8 %)**, src/esp32 66, wpa_supplicant 41,5, driver 34 |
| **E22-DevKitC** (classic) | SDK-Blob 348 (17,9 %; classic hat zusätzlich BT-Controller und phy-Quellen 37 KB), src-Kern 291, web_functions 197 (10,1 %), mbedtls 154, newlib 125, lwip 100, NimBLE 78, eh_frame 70 |
| **t_deck_plus** (S3+LVGL) | SDK-Blob 353 (14,4 %), src-Kern 309, **lvgl 245 (9,9 %)**, web_functions 207, mbedtls 137, newlib 133, **ESP32-audioI2S 100 (4,1 %) plus ca. 47 KB Dekoder-Tabellen ohne DWARF-Ort**, lwip 94, eh_frame 85, NimBLE 75, src/t-deck 74 |
| **heltec_t114** (nRF) | src-Kern 245 (38,4 %), **src/web_functions 167,5 (26,3 %)**, src/nrf52 45, newlib-nano 31, CryptoCell (Adafruit_nRFCrypto) 31, TinyUSB 22, Arduino-Core 17, libm 15,5, Bluefruit 13, LittleFS 10, **RAK13800-W5100S 9,7** |
| **t_echo** (nRF) | wie T114: src-Kern 251, web_functions 168 (25,4 %), src/nrf52 58 (davon 20 KB Rodata, vermutlich `t_echo_images.h`), newlib-nano 31, CryptoCell 31 |

Größte Einzeldateien: web_functions.cpp 135 KB (davon Strings 87 KB), command_functions.cpp 73 KB (Hilfetexte 21 KB), web_rm_page.cpp 46 KB (45 KB reine Strings), newlib vfprintf 48 KB, mdns.c 21 KB, vfscanf 18 KB, mbedtls error.c 14,8 KB, ssl_ciphersuites.c 10 KB; T-Deck: Audio.cpp 46, lv_obj_functions 44, aac_decoder 30, lodepng 22 (PNG-Kartenkacheln: nötig), Montserrat 12/14/16/18 = 63,6 KB. SDK-Blob auf S3 (T-Connect-Pro-Map): net80211 114 K, btdm_app 68 K, pp 47 K, phy 33 K, coexist 5 K. Nicht änderbar außer über eigenes SDK.

## 3. SIZE-05 (Ethernet und Web auf nRF52 ohne Ethernet): Verdikt

**Zahl: heltec_t114 192,2 KB, t_echo 188,1 KB** frei, wenn nur die Aufrufschnittstelle gekappt wird (29,5 % bzw. 27,8 % des Images, 23 % des 815-KB-Limits). Mit Gating auch von `extudp_functions`/`udp_frame_nrf52`: **203,6 bzw. 199,3 KB**. Obergrenze etwa 219 KB (T114), weil weitere 26,8 KB Strings der Web-Objekte der Analyse als „unerreicht“ gelten (Zeiger nur aus nicht verfolgten Funktionen), vermutlich ebenfalls frei. Der Kollege liegt mit „bis 187 KB“ als Objektsumme richtig, die 42 KB sind nur Code.

Herleitung T114 (Map, exakt je Objekt): web_functions.cpp.o 112,7 K (Code 37,8 / Strings+Tabellen 74,9), web_rm_page 47,4 (Code 2,0 / Strings 45,3), web_setup 7,3, rm_handlers 2,6, txbuf 1,1 = **171,5 KB Web, davon Code 49,6 KB und anonyme Strings 121,9 KB**; nrf_eth 7,3; RAK13800-W5100S 9,9; gateway_service/udp_drain 1,2; Summe **190,0 KB**. Die Lücke 42 → 187 KB sind die **122 KB Strings**: Sie haben kein `nm`-Symbol (gemergte `.rodata.str1.1`).

Call-Graph-Modell (`reach_nrf.py`: Funktionsgraph aus `bl/b` plus Literal-Pool-Zeigern, Wurzeln Vektortabelle und `.data`; Kanten von außen in die Menge gekappt). Die Schnittstelle ist schmal: `command_functions`→`ethStat`; `nrf52_main`→`gatewayService_nrf52`, 10 NrfETH-/eth*-Aufrufe, `startWebserver`/`loopWebserver`; `extudp_functions`→8× `EthernetUDP`; Static-Init-Einträge. Zusätzlich frei fallen **abhängige Exklusiv-Objekte**: config_json 12,2 K, udp_frame_nrf52 6,4 K, rm_runtime 1,9 K, nrf52_flash 1,2 K, libm-pow 0,9 K, ntp_async 0,8 K. Aufteilung T114: Code 86,8 K, Strings 99,1 K, Rodata 6,3 K. Auch `Print::flush` (Adafruit-GFX) wird derzeit aus EthernetServer.cpp.o bedient. Modellunsicherheit ±10 KB (52,6 KB Image im Basisfall nicht erreichbar modelliert, vor allem Strings), **kein Link-Lauf**; ein Gating-Build verifiziert es in 10 Minuten. wiscore_rak4631 hat die HW (RAK13800): nicht anwendbar (Modell würde 174 KB liefern).

Danach: T114 458 KB (56 %), t_echo 488 KB (60 %).

## 4. Hochrechnung SIZE-01/02/03 auf alle ESP32-Envs

Kalibrierung aus den gemessenen V3-Deltas des Kollegen: SIZE-01 = 1,322 × (eh_frame + gcc_except_table) (T-Deck-Gegenprobe: 133,1 gegen 136,0 KB gemessen), SIZE-02 = 197 B je `[E][`-Logzeile, SIZE-03 = 36,3 B je Canary-Funktion (V3: 600 Funktionen, ermittelt aus `l32r`-Ladungen von `__stack_chk_fail`). Kombi mit −4,5 % Überlapp (V3 gemessen 160,2 gegen Summe 167,7 KB). Classic-LX6-Werte sind aus S3 **extrapoliert** (±15 %), die EH-Größen selbst sind gemessen. Linkerlage: `__XT_EXCEPTION_TABLE_`→`__eh_frame` = gcc_except_table, `__eh_frame`→`__init_array_start` = eh_frame (ESP32-`sections.ld`).

| Env | EH-Daten (eh_frame + except_table) | SIZE-01 | SIZE-02 ([E]-Zeilen) | SIZE-03 (Canary-Fkt) | Kombi* | % Image |
|---|---:|---:|---:|---:|---:|---:|
| t_deck | 84.7+13.6 KB | 130 KB | 46 KB (239) | 24 KB (676) | **191 KB** | 7.8 % |
| t_deck_plus | 84.7+13.6 KB | 130 KB | 46 KB (239) | 24 KB (676) | **191 KB** | 7.8 % |
| ttgo_tbeam_supreme | 74.4+11.2 KB | 113 KB | 44 KB (228) | 21 KB (599) | **170 KB** | 9.0 % |
| LilyGo_T_Connect_Pro | 76.7+11.5 KB | 117 KB | 40 KB (206) | 22 KB (610) | **170 KB** | 8.8 % |
| vision-master-e290 | 77.9+11.8 KB | 119 KB | 35 KB (184) | 22 KB (613) | **168 KB** | 8.9 % |
| ttgo_tbeam_SX1262 | 74.0+11.1 KB | 112 KB | 43 KB (221) | 19 KB (531) | **166 KB** | 8.4 % |
| ttgo_tbeam_SX1268 | 74.0+11.1 KB | 112 KB | 43 KB (221) | 19 KB (531) | **166 KB** | 8.4 % |
| ttgo_tbeam | 73.6+11.1 KB | 112 KB | 43 KB (221) | 19 KB (524) | **165 KB** | 8.4 % |
| T-ETH-ELITE_1262 | 72.6+11.2 KB | 111 KB | 40 KB (206) | 21 KB (596) | **164 KB** | 8.7 % |
| heltec_wireless_tracker | 73.7+11.3 KB | 112 KB | 36 KB (186) | 21 KB (600) | **162 KB** | 8.6 % |
| vision-master-e213 | 73.3+11.2 KB | 112 KB | 35 KB (184) | 21 KB (597) | **161 KB** | 8.7 % |
| E22_XML-DevKitC | 74.6+11.4 KB | 114 KB | 35 KB (180) | 18 KB (521) | **159 KB** | 8.1 % |
| LilyGo_T3_S3_V1_3 | 70.2+11.3 KB | 108 KB | 37 KB (193) | 21 KB (602) | **159 KB** | 8.5 % |
| LilyGo_T-Beam-1W | 70.3+11.3 KB | 108 KB | 37 KB (191) | 21 KB (601) | **159 KB** | 8.5 % |
| wireless-paper | 72.1+10.8 KB | 110 KB | 35 KB (181) | 21 KB (592) | **158 KB** | 8.7 % |
| E22_1262_S3-DevKitC-1-N16R8 | 70.2+11.2 KB | 108 KB | 36 KB (188) | 21 KB (600) | **158 KB** | 8.4 % |
| E22_1268_S3-DevKitC-1-N16R8 | 70.2+11.2 KB | 108 KB | 36 KB (188) | 21 KB (600) | **158 KB** | 8.4 % |
| heltec_wifi_lora_32_V4 | 70.8+11.2 KB | 108 KB | 35 KB (184) | 21 KB (599) | **158 KB** | 8.5 % |
| heltec_wifi_lora_32_V3 | 70.2+11.2 KB | 108 KB | 35 KB (181) | 21 KB (600) | **156 KB** | 8.4 % |
| heltec_wireless_stick | 70.1+11.2 KB | 108 KB | 35 KB (181) | 21 KB (600) | **156 KB** | 8.5 % |
| esp32-loraprs-e22 | 69.8+10.9 KB | 107 KB | 35 KB (180) | 18 KB (520) | **153 KB** | 7.9 % |
| E22_1262-DevKitC | 69.8+10.9 KB | 107 KB | 35 KB (180) | 18 KB (519) | **153 KB** | 7.9 % |
| E22-DevKitC | 69.8+10.9 KB | 107 KB | 35 KB (180) | 18 KB (519) | **153 KB** | 7.9 % |
| heltec_wifi_lora_32_V2 | 69.5+11.0 KB | 106 KB | 35 KB (180) | 18 KB (510) | **152 KB** | 7.9 % |
| ttgo-lora32-v21 | 69.4+10.9 KB | 106 KB | 35 KB (180) | 18 KB (511) | **152 KB** | 7.8 % |
| esp32-loraprs-ra01 | 69.5+11.0 KB | 106 KB | 34 KB (176) | 18 KB (510) | **151 KB** | 7.8 % |
| t_deck_pro | 82.4+11.8 KB | 124 KB | 0 KB (0) | 23 KB (645) | **141 KB** | 6.3 % |
| esp32-safeboot / esp32-S3-safeboot | 0 + 0,2 KB (eh_frame verworfen) | 0 | 0 (keine [E][-Strings) | 0 (kein `__stack_chk`) | **0** | 0 |
Safeboot: **alle drei Flags wirkungslos.** Tasmota/IDF 5 verwirft `.eh_frame` (Map: `.eh_frame 0x0`), `.xt_except_table` nur 176 B, keine `[E][`-Strings, kein `__stack_chk`-Symbol, `-Oz` und LTO schon aktiv. Die Annahme „`-fno-exceptions`/`CORE_DEBUG_LEVEL=0` hilft dort“ ist widerlegt; der Hebel dort ist ota.html (Abschnitt 6, M01).

## 5. Web-Strings und gzip-Einbettung

Gemessen mit Regex über die Quellen (adjazente Literale zusammengefasst, Escapes aufgelöst): web_functions.cpp 1829 Literalgruppen, **95,5 KB**; web_rm_page.cpp 167 Gruppen, **45,4 KB**; web_setup/rm_handlers/txbuf/UIComponents zusammen unter 4 KB (nur kurze Strings). Im ELF (V3) belegen sie 87,3 + 45,3 KB. Der Text ist bereits minifiziert (Whitespace-Potenzial unter 1 KB, JS ohne Kommentare).

| Klasse (Gruppen ≥48 B) | web_functions roh → gzip -9 | web_rm_page roh → gzip -9 |
|---|---|---|
| CSS statisch | 7,9 → 2,8 KB | 3,9 → 1,7 KB |
| JS statisch | 18,1 → 6,0 KB | 23,6 → 8,9 KB |
| HTML statisch | 28,0 → 10,0 KB | 8,8 → 3,2 KB |
| sonstige statisch | 7,5 → 3,5 KB | 6,4 → 2,9 KB |
| mit `%`-Platzhalter oder `+`-Verkettung (dynamisch) | 13,2 KB (bleibt) | 2,5 KB (bleibt) |

Statisch gesamt 104 KB roh → **ca. 39 KB gzip**, als zusammenhängende Blobs also **−65 KB** (3,4 % V3). Jedes Literal einzeln gzippen bringt fast nichts (53,8 + 32,6 KB für 113 KB): nur Blob-Ebene hilft. **Realistisch: CSS+JS (53,5 KB roh) als zwei gzip-Ressourcen `/mc.css`, `/rm.js` → −34 KB**, plus ggf. HTML-Gerüste pro Seite (−10 bis −20 KB, aufwendig).

Haken: (1) Seiten werden heute streamend mit `print/println` aus Literalen ≤512 B gebaut (RAK4631 cappt einen Write bei 2048 B, nRF-printf liest >255 B über); gz-Blobs brauchen einen `write(buf,len)`-Pfad in Chunks mit eigenem Header (`Content-Encoding: gzip`, `Content-Length`, `Cache-Control`). (2) Sub-Seiten laufen per `innerHTML`, Skripte darin laufen nicht: JS/CSS müssen als `<script src>`/`<link>` einmal in der Hülle stehen; zusätzliche Requests am Einzelthread-Server. (3) `tools/webgui_rm_test.js` extrahiert die Literale aus dem C++-Quelltext: Quellen müssten als `.js/.css` im Repo liegen, Test und Generator-Schritt (`pre:`-Script, deterministisch `mtime=0`) umgestellt werden. (4) Dynamische Werte (Rufzeichen, Zustände) müssen per `/api`-JSON geholt statt eingefügt werden. (5) Browser: gzip-Content-Encoding ist universell, kein Kompatibilitätsrisiko; RAM/CPU auf dem Node null (Daten bleiben `const` im Flash). **Aufwand mittel bis hoch (2–4 Tage mit Tests).** Wert: ESP32 −34 KB (1,8 %), nRF nur für wiscore sinnvoll (86 %, hat Ethernet-HW, −34 KB = 4,2 % des Limits); für T114/t_echo erledigt SIZE-05 das Thema.

**Sofort umsetzbarer Sonderfall Safeboot:** `ota.html` 36 639 B → gzip -9 **11 700 B** (−24 939 B). `ElegantOTA.cpp:99/108` enthält `Content-Encoding: gzip` schon auskommentiert, `convert.sh` müsste `gzip -9n` vor `xxd -i` setzen; `main.cpp` hat nur 0,5 KB weitere Literale. Classic-Safeboot: Headroom 7,3 → 31,7 KB (4,5 %).

## 6. Neue Befunde (zusätzlich zu SIZE-01…17)

| ID | Env | Befund | Größe | Risiko |
|---|---|---|---|---|
| **SIZE-M01** | esp32-safeboot, S3-safeboot | `ota.html` roh eingebettet; gzip-Header schon vorbereitet | **−24,9 KB** (M) | niedrig |
| **SIZE-M02** | Safeboot | SIZE-01/02/03 dort wirkungslos (IDF 5 verwirft eh_frame, keine Logstrings, kein Canary). Offen: mDNS 23 KB (Map: `mdns` 23,5/23,0 KB), `libbt` 5,3 KB classic | 0 / −23 KB | mDNS: Feature |
| **SIZE-M03** | T114, t_echo | SIZE-05 zieht abhängigen Code mit (config_json 12,2 K, udp_frame_nrf52 6,4 K u. a. = ca. 24 KB), nicht in „187 KB“ | in 192 KB enthalten | – |
| **SIZE-M04** | 3× nRF | newlib-nano/libm 47 KB: strtod/dtoa/mprec/gdtoa ca. 10 KB, `*_rem_pio2` ×4 (Float und Double) 6,9 KB, strftime/mktime/tzset 5,1 KB, Double-Soft-Float (libgcc) 4,3 KB | 10–15 KB (N) | mittel (Float-Pfade) |
| **SIZE-M05** | 3× nRF | CryptoCell `nrf_cc310` 31 KB, gezogen von `BLESecurity` (ECC/LESC) und `phone_commands` (Hash) | nicht ohne BLE-Pairing-Umbau | hoch |
| **SIZE-M06** | alle ESP32 | newlib `vfprintf` 48 KB + `vfscanf` 18 KB = 66 KB (3,5 %); `sscanf` in 14 Quelldateien (67 Fundstellen) | bis 18 KB (nur bei vollständigem sscanf-Ersatz) | Aufwand hoch |
| **SIZE-M07** | alle ESP32 | zwei parallele Settings-Tabellen `cfg_fields` 5,5 KB + `settings_schema::kFields` 4,8 KB + `meshcom_settings` 2,1 KB; `FW_UPDATE_ROOTS_PEM` 6,4 KB (PEM statt DER, in SIZE-04 enthalten) | ca. 5 KB (N) | mittel |
| **SIZE-M08** | t_deck, t_deck_plus | Audio-Dekoder-Tabellen (huffTable 8,5 K, kbd/sin/cos4sin4 je 4,6 K, noiseTab 4,1 K u. a. ca. 47 KB) hat nm nicht dem Lib-Bucket zugeordnet: Audio gesamt ca. 147 KB (5,8 %), AAC ca. 55–60 KB (Schätzung); Quelle nur SD (`connecttoFS`) | 55–77 KB | mittel (SD-Sounds) |
| **SIZE-M09** | alle ESP32 | IDF-`espcoredump` 10 KB (V3, E22), `wifi_provisioning` 3 KB, `bt` 3–5 KB, nur über eigene sdkconfig | ca. 16 KB (N) | Migration |

Bestätigt (Gegenprobe): SIZE-06 Sensorbibliotheken 22 KB Lib-Code (BME680 6,2, BMP3XX 4,0, BusIO 2,1 …) + Glue = 38 KB plausibel; SIZE-11 mdns 22,9 KB; SIZE-14 TinyUSB Video 3,3 + MSC 2,7 + HID 0,9 + MIDI 0,8 + Vendor 0,5 + WebUSB 0,4 + MAX3421-Host 0,85 ≈ 9,5–11 KB.

## 7. Priorisierte Hebel (Ersparnis × Risiko, alle 32 Envs)

| # | Hebel | Envs | Ersparnis | Risiko | Aufwand |
|---|---|---|---|---|---|
| 1 | **SIZE-05** HAS_ETHERNET für nRF ohne ETH-HW | T114, t_echo | **192–204 KB** (28–30 % Image) | mittel/hoch (Betreiber, ca. 5 Stellen) | M |
| 2 | **M01** ota.html gzip | 2 Safeboot | **24,9 KB** (classic: Headroom ×4,3) | niedrig | S |
| 3 | **SIZE-01** `-fno-exceptions` | 27 ESP32 | 106–130 KB (5,4–5,9 %) | niedrig/mittel | S |
| 4 | **SIZE-02** `CORE_DEBUG_LEVEL=0` | 26 ESP32 (t_deck_pro schon) | 34–46 KB | niedrig | S |
| 5 | **SIZE-07a+b** LVGL-Widgets, TFT_eSPI-Fonts | 2 T-Deck | 56,7 KB | niedrig | S |
| 6 | **SIZE-14** TinyUSB nur CDC (+ **SIZE-15, -13**) | 3 nRF | 11 (+4,3 +2,2) KB, wiscore danach ca. 122–128 KB Headroom | niedrig | S |
| 7 | **SIZE-04** AU/TLS | 27 ESP32 | 161 KB (V3) | **hoch** (Produktfunktion) | M |
| 8 | **Web-CSS/JS gzip** (Abschnitt 5) | 27 ESP32 + wiscore | 34 KB (max. 65) | mittel | L |
| 9 | **SIZE-07c/M08** Audio-AAC/FLAC | 2 T-Deck | 55–77 KB | mittel | M |
| 10 | **SIZE-06** Sensoren, **SIZE-09** GPS, **SIZE-12** U8g2 | je Variante | 38 / 24,5 / 19,8 KB | mittel (Feldnutzer) | S–M |
| 11 | **SIZE-11** mDNS (auch Safeboot) | 27 + 2 | 23,8 KB | mittel | S |
| 12 | **SIZE-03** ohne Canary | 27 ESP32 | 18–24 KB | Sicherheit | S |
| 13 | SIZE-10 KISS, SIZE-08 NimBLE (4 Envs), SIZE-16/17, SIZE-M04/M07 | verschieden | 2–15 KB | niedrig bis mittel | S–M |

Lesart nach Engpass: **Safeboot classic** (1 %): #2, danach mDNS. **wiscore_rak4631** (13,9 %): #6 (≈ +17 KB), #8, M04, SIZE-16: zusammen rund 60 KB. **t_echo/T114**: #1 allein macht aus 17/20 % Headroom 40/44 %. ESP32-Apps haben 41–45 % Headroom (T-Deck 80 %); dort zählt nur Image-/OTA-Größe: #3+#4 zusammen ≈ 140–175 KB je Env (7–9 %), SIZE-04 ist der einzige weitere große Hebel.


## Anhang A: Top-Buckets und Top-25-Dateien je Repräsentant

**heltec_wifi_lora_32_V3** (Summe 1896117 B, DWARF+Literal-Analyse)

Buckets: SDK-Blob (ohne DWARF) 308.4 KB (16.7 %); src (Kern) 302.6 KB (16.3 %); src/web_functions 205.5 KB (11.1 %); SDK/idf:mbedtls 139.0 KB (7.5 %); libc (newlib) 122.9 KB (6.6 %); SDK/idf:lwip 93.2 KB (5.0 %); libdeps/NimBLE-Arduino 72.8 KB (3.9 %); <eh_frame> 70.2 KB (3.8 %); src/esp32 65.7 KB (3.5 %); SDK/idf:wpa_supplicant 41.5 KB (2.2 %); SDK/idf:driver 34.0 KB (1.8 %); framework/arduino-core 28.4 KB (1.5 %)

Top-25 Dateien (KB): src/web_functions/web_functions.cpp 134.5; src/command_functions.cpp 72.8; <eh_frame> 70.2; newlib/stdio/vfprintf.c 48.3; src/web_functions/web_rm_page.cpp 46.4; src/loop_functions.cpp 33.1; src/esp32/esp32_main.cpp 30.2; src/lora_functions.cpp 23.9; idf/mdns/mdns.c 21.1; src/nbr_matrix.cpp 20.6; newlib/stdio/vfscanf.c 17.8; src/web_functions/web_setup.cpp 14.8; idf/mbedtls/mbedtls/library/error.c 14.8; src/esp32/fw_update_net.cpp 13.2; src/udp_functions.cpp 12.7; src/rm_runtime.cpp 11.8; <gcc_except_table> 11.2; src/extudp_functions.cpp 10.4; <unaccounted (pad/pool gaps/unsized)> 10.1; idf/mbedtls/mbedtls/library/ssl_ciphersuites.c 10.0; idf/lwip/lwip/src/api/sockets.c 9.9; idf/driver/uart.c 9.7; idf/wpa_supplicant/src/common/sae.c 9.5; fw/libraries/WiFi/src/WiFiGeneric.cpp 9.4; idf/mbedtls/mbedtls/library/ssl_tls.c 9.0

**E22-DevKitC** (Summe 1990713 B, DWARF+Literal-Analyse)

Buckets: SDK-Blob (ohne DWARF) 348.5 KB (17.9 %); src (Kern) 290.9 KB (15.0 %); src/web_functions 196.7 KB (10.1 %); SDK/idf:mbedtls 153.9 KB (7.9 %); libc (newlib) 125.5 KB (6.5 %); SDK/idf:lwip 99.7 KB (5.1 %); libdeps/NimBLE-Arduino 77.6 KB (4.0 %); <eh_frame> 69.8 KB (3.6 %); src/esp32 65.1 KB (3.3 %); SDK/idf:wpa_supplicant 44.4 KB (2.3 %); SDK-Blob (phy, mit DWARF) 37.3 KB (1.9 %); SDK/idf:driver 34.6 KB (1.8 %)

Top-25 Dateien (KB): src/web_functions/web_functions.cpp 126.7; src/command_functions.cpp 71.0; <eh_frame> 69.8; newlib/stdio/vfprintf.c 48.5; src/web_functions/web_rm_page.cpp 46.4; src/loop_functions.cpp 33.4; src/esp32/esp32_main.cpp 31.2; src/lora_functions.cpp 22.7; idf/mdns/mdns.c 22.5; src/nbr_matrix.cpp 19.8; newlib/stdio/vfscanf.c 17.8; <unaccounted (pad/pool gaps/unsized)> 14.9; idf/mbedtls/mbedtls/library/error.c 14.8; /chip7.1_phy/chip_7.1/board_code/app_test/pp/phy/phy_chip_v7.c 14.7; src/web_functions/web_setup.cpp 13.9; src/esp32/fw_update_net.cpp 13.1; p7.1_phy/chip_7.1/board_code/app_test/pp/phy/phy_chip_v7_cal.c 13.0; src/rm_runtime.cpp 11.8; src/udp_functions.cpp 11.8; <gcc_except_table> 10.9; idf/lwip/lwip/src/api/sockets.c 10.7; src/extudp_functions.cpp 10.4; idf/mbedtls/mbedtls/library/ssl_ciphersuites.c 10.0; idf/driver/uart.c 10.0; idf/wpa_supplicant/src/common/sae.c 9.9

**t_deck_plus** (Summe 2518805 B, DWARF+Literal-Analyse)

Buckets: SDK-Blob (ohne DWARF) 353.1 KB (14.4 %); src (Kern) 308.6 KB (12.5 %); lib/lvgl 244.5 KB (9.9 %); src/web_functions 207.3 KB (8.4 %); SDK/idf:mbedtls 137.3 KB (5.6 %); libc (newlib) 133.3 KB (5.4 %); lib/ESP32-audioI2S 99.7 KB (4.1 %); SDK/idf:lwip 93.5 KB (3.8 %); <eh_frame> 84.7 KB (3.4 %); libdeps/NimBLE-Arduino 75.0 KB (3.0 %); src/t-deck 74.1 KB (3.0 %); src/esp32 73.3 KB (3.0 %)

Top-25 Dateien (KB): src/web_functions/web_functions.cpp 135.7; <eh_frame> 84.7; src/command_functions.cpp 76.8; newlib/stdio/vfprintf.c 48.5; src/web_functions/web_rm_page.cpp 46.9; lib/ESP32-audioI2S/src/Audio.cpp 46.4; src/t-deck/lv_obj_functions.cpp 43.6; src/esp32/esp32_main.cpp 32.4; lib/ESP32-audioI2S/src/aac_decoder/aac_decoder.cpp 30.1; src/loop_functions.cpp 29.7; src/lora_functions.cpp 24.6; lib/lvgl/src/extra/libs/png/lodepng.c 22.3; idf/mdns/mdns.c 21.1; src/nbr_matrix.cpp 20.6; lib/lvgl/src/font/lv_font_montserrat_18.c 18.6; newlib/stdio/vfscanf.c 17.8; lib/lvgl/src/font/lv_font_montserrat_16.c 15.5; idf/mbedtls/mbedtls/library/error.c 14.7; lib/ESP32-audioI2S/src/mp3_decoder/mp3_decoder.cpp 14.7; src/web_functions/web_setup.cpp 14.7; <gcc_except_table> 13.6; src/esp32/fw_update_net.cpp 13.5; lib/lvgl/src/font/lv_font_montserrat_14.c 13.3; src/udp_functions.cpp 13.0; idf/fatfs/src/ff.c 12.6

**heltec_t114** (Summe 653245 B, Map-File)

Buckets: src (Kern) 244.9 KB (38.4 %); src/web_functions 167.5 KB (26.3 %); src/nrf52 44.9 KB (7.0 %); libc (newlib) 30.7 KB (4.8 %); framework/lib:Adafruit_nRFCrypto 30.6 KB (4.8 %); libdeps/Adafruit_TinyUSB_Arduino 22.0 KB (3.4 %); framework/arduino-core (nRF52) 17.1 KB (2.7 %); libm 15.5 KB (2.4 %); libdeps/Bluefruit52Lib 13.3 KB (2.1 %); libdeps/Adafruit_LittleFS 10.3 KB (1.6 %); libdeps/RAK13800-W5100S 9.7 KB (1.5 %); libdeps/SX126x-Arduino 8.6 KB (1.4 %)

Top-25 Dateien (KB): src/web_functions/web_functions.cpp 110.1; src/command_functions.cpp 54.5; src/web_functions/web_rm_page.cpp 46.3; src/loop_functions.cpp 25.3; src/lora_functions.cpp 20.6; src/nbr_matrix.cpp 18.4; src/config_json.cpp 14.2; src/rm_runtime.cpp 13.6; src/nrf52/nrf52_main.cpp 11.6; src/extudp_functions.cpp 9.9; src/nrf52/nrf52_flash.cpp 8.9; libdeps/Adafruit_LittleFS/littlefs/lfs.c 8.5; src/rm_exec_read.cpp 8.3; src/web_functions/web_setup.cpp 7.1; src/nrf52/nrf_eth.cpp 7.1; src/aprs_functions.cpp 6.9; src/nrf52/udp_frame_nrf52.cpp 6.7; src/gps_functions.cpp 6.2; src/remote_cmd.cpp 5.1; src/txring_functions.cpp 5.1; src/settings_schema.cpp 4.5; src/nbr_views.cpp 3.7; libdeps/SX126x-Arduino/radio/sx126x/radio.cpp 3.7; src/msgstore.cpp 3.7; libdeps/Adafruit_TinyUSB_Arduino/device/usbd.c 3.7

**t_echo** (Summe 678637 B, Map-File)

Buckets: src (Kern) 250.9 KB (37.9 %); src/web_functions 168.2 KB (25.4 %); src/nrf52 57.6 KB (8.7 %); libc (newlib) 30.7 KB (4.6 %); framework/lib:Adafruit_nRFCrypto 30.6 KB (4.6 %); libdeps/Adafruit_TinyUSB_Arduino 22.0 KB (3.3 %); framework/arduino-core (nRF52) 17.1 KB (2.6 %); libm 16.7 KB (2.5 %); libdeps/Bluefruit52Lib 13.3 KB (2.0 %); libdeps/Adafruit_LittleFS 10.3 KB (1.6 %); libdeps/RAK13800-W5100S 9.7 KB (1.5 %); libdeps/SX126x-Arduino 8.6 KB (1.3 %)

Top-25 Dateien (KB): src/web_functions/web_functions.cpp 110.5; src/command_functions.cpp 54.1; src/web_functions/web_rm_page.cpp 46.3; src/loop_functions.cpp 29.0; src/lora_functions.cpp 20.6; src/nbr_matrix.cpp 18.4; src/config_json.cpp 14.2; src/rm_runtime.cpp 13.6; src/nrf52/nrf52_functions.cpp 12.2; src/nrf52/nrf52_main.cpp 12.1; src/extudp_functions.cpp 9.9; src/nrf52/nrf52_flash.cpp 8.9; libdeps/Adafruit_LittleFS/littlefs/lfs.c 8.5; src/rm_exec_read.cpp 8.3; src/web_functions/web_setup.cpp 7.4; src/nrf52/nrf_eth.cpp 7.1; src/aprs_functions.cpp 6.9; src/nrf52/udp_frame_nrf52.cpp 6.7; src/gps_functions.cpp 6.5; src/remote_cmd.cpp 5.1; src/txring_functions.cpp 5.1; src/settings_schema.cpp 4.5; src/nbr_views.cpp 3.7; libdeps/SX126x-Arduino/radio/sx126x/radio.cpp 3.7; src/msgstore.cpp 3.7

