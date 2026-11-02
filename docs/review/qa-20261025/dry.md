# DRY-Review fork-dev (HEAD f0a81c8), read-only

Methode: Das Agent-Tool stand nicht zur Verfuegung, ich habe selbst gelesen. Dazu ein eigener Klon-Scan (`review/dupscan.py`, normalisierte 5-6-Zeilen-Fenster) und `tools/arch_duplication.py`. Jede Fundstelle habe ich selbst gelesen. Groessen sind nach Moeglichkeit gemessen: `nm -S` auf `scratchpad/size/E22_1262-DevKitC/firmware.elf` (ESP32, vorhandener Build, kein pio). Alles andere ist **Schaetzung**.

ID-Praefix `DRY-R`: `DRY-20..25` sind im Defektkatalog schon vergeben (20 Batterie, 21 nrf_eth/udp, 22 serial twin, 23 enqueueTx, 24 Toggles, 25 I2C-Guard).

Grundsatz: Flash spart nur Duplikat **im selben Binary**. ESP32-gegen-nRF52 und Board-gegen-Board (t-deck-pro/t5, VisionMaster/WirelessPaper) sparen 0 Byte. Dort lohnt es nur wegen Drift.

Schwere = Konsolidierungsnutzen (Wartung + Flash). Konfidenz bezieht sich auf die Dopplung und die Flash-Schaetzung.

## 1. Befunde

| ID | Schw. | Fundstellen | Dupliziert | Ersparnis | Vorschlag | Risiko | Status / Konf. |
|---|---|---|---|---|---|---|---|
| DRY-R01 | Hoch | `src/web_functions/web_setup.cpp:24-922` (setParam), `:924-1311` (getParam) | 72 Rungen gleicher Form (54x `snprintf("--cmd %s")` + `commandAction` + Readback + `returnValue`); `getParam` ist das Readback-Spiegelbild | ELF: setParam 9906 B + getParam 2746 B. Geschaetzt -300 Z., **-4..7 KB** pro ESP32-Web-Env | Statische Tabelle `{name, cmd, Typ int/float/onoff/str, Zeiger oder Getter}` plus eine Engine fuer set und get. Sonderfaelle (setcall, setvia, tz, aumode, tempoffset*, netmode, manualcommand) bleiben Einzelrungen | Mittel. Web-API-Strings und Float-Formate (`String(x,1)`, `,3`) muessen identisch bleiben. `test/golden/producer_match_lint.py` greift `paramName.equals("x")` per Regex und braucht eine Anpassung. Settings-Layout und Wire unberuehrt | Neu (04-doc nennt nur CC 155); hoch / mittel |
| DRY-R02 | Mittel | `command_functions.cpp`: setown* `:4124-4253`, setssid/setpwd `:3954-4010`, hamnet/net `:4263-4362`, Arg-Kopie+LF-Strip `:1234, 2890, 2935, 3383, 3704, 3731, 3780, 3806`, `rebootAuto`-Guard 25x | 5+2 String-Setter mit identischem 20-Zeilen-Block (`msg_text[50]=0`, snprintf, `bWifiSetting`, save, 3-fach-strlen-Reboot-Check); 5 `node_hamnet_only`-Rungen; 8x Arg-Kopie | -150 Z., ca. **-0.8..1.5 KB** (commandAction = 36 430 B, Anteil geschaetzt) | `cmdSetNetStr(dst,size,off)`, `cmdAutoReboot(ms)`, `cmdArgCopyTrim()`. Toggles und Numerik sind per `command_toggles.h` / `command_setters.h` schon erledigt | Niedrig. **Nebenbefund:** `_owner_c[strlen(_owner_c)-1]` ist bei leerem Argument ein Zugriff auf Index -1 (UB); ein Helper behebt das | Neu (Rest von DRY-24/SIMP-26); hoch / mittel |
| DRY-R03 | Mittel | `esp32/esp32_main.cpp:2156-2272` (Setup, 4 Chip-Zweige), `:3577, 3682, 3818, 3989, 4013`, `:5421-5478` (3x) | RX-Re-Arm (clear*Action, setPacketReceivedAction, RADIO_CTRL HIGH, `startReceive(...)`) 12x | -80 Z., **-0.3..0.5 KB** | 2 Helper (Action vor / nach `startReceive`); nur byte-gleiche Kopien zusammenfassen | **Hoch**: Produktions-RX-Pfad. Die Reihenfolge ist absichtlich verschieden (Kommentar "Radio in RX zuerst", IRQ-Race). Nur `:5448/:5470` und `:3989/:4013` anfassen, oder lassen | Neu; hoch / niedrig (Flash) |
| DRY-R04 | Mittel | `lora_functions.cpp:3075-3112` (track), `:3119-3148` (aprs), `:3184-3215` (msg) | TX-Start 3x: RAK-Zweig (`vTaskSuspendAll`+`Radio.Send`) und RadioLib-Zweig (RADIO_CTRL, `enablePATransmit`, `startTransmit`, Fehler -> Rollback) | -45 Z., **-0.2..0.3 KB** | `static bool radioStartTx(kind)`. Unterschiede nur LED-Farbe und Log-Text | Hoch/Mittel: TX-Pfad. Bei Aenderung PA-Reihenfolge (Heltec V4) pruefen | Neu; hoch / niedrig |
| DRY-R05 | Mittel | `extudp_functions.cpp:781-830` (c_json und c_tjson), `:934-944` (sendExternJson), `:1022-1024` (sendExternNotice) | beginPacket/write/Reset-bei-Fehler/endPacket 4x. Dazu die Halbier-Log-Logik 2x (`:789-794`, `:816-821`) | ELF sendExtern 4755 B, Notice 607 B. -35 Z., **-0.3..0.5 KB** | `static bool externTx(tag, buf, len)` | Niedrig. Die Logformate `[EXT] Out:/Tele-Out:/Ack-Out:` bleiben byte-gleich, `tools/` und Golden parsen sie. `sendExternNotice` setzt bei Fehler nicht zurueck: Absicht klaeren | Neu; hoch / mittel |
| DRY-R06 | Mittel | `MyClock -> node_date_*` (6 Felder) in `command_functions.cpp:1134`, `gps_functions.cpp:1151,1296`, `phone_commands.cpp:403`, `time_functions.cpp:145`, `topo_ui.cpp:212`, `esp32_main.cpp:4154`, `nrf52_main.cpp:1453`, `t-deck-pro/tdeck_pro.cpp:639`. Set-Clock-Block (RTC + MyClock) in `command_functions.cpp:1090-1141` und `phone_commands.cpp:370-410` | 9x 6 Zuweisungen; 2x 40 Zeilen Zeit setzen (Unterschied: Jahr >2023 gegen >2020, Quelle "HAND" gegen `bPhoneTimeValid`) | -60 Z., **-0.3..0.5 KB** (je Image) | `syncNodeDateFromClock()` und `setClockFromYMDHMS(src)` in `time_functions.cpp`. `esp32_main:4154` (Jahr<=2023 -> 2000) und die Schwellen als Parameter lassen | Niedrig | Neu; hoch / mittel |
| DRY-R07 | Mittel | `src/hmac_sha256.cpp` (nm: compress 396 B, K 256, Final 206, hmacSha256 315, Update/Init/sha256 ca. 270) gegen `mbedtls_sha256_*` (im Image wegen TLS/OTA). Weitere Nutzer: `fw_update_net.cpp:951,1059,1097` (Streaming), `remote_cmd.cpp:252,262,537`, `rm_runtime.cpp:366`. Dritte Variante: `external_radio_glue.cpp:133` (`mbedtls_md_hmac`) | zwei SHA-256-Implementierungen im ESP32-Image | **ca. -1.3 KB** pro ESP32-Env (29), 0 auf nRF52 | `#if defined(ESP32)`: `sha256/hmacSha256` und Streaming auf mbedtls abbilden (`Sha256Ctx` kapselt den mbedtls-Kontext). nRF52 und Host behalten die Eigenimplementierung | Mittel: RM-Auth und OTA-Hash. HMAC ist standardisiert, also Wire-neutral, aber `test_hmac_sha256` deckt nur den portablen Pfad ab; Bench fuer ESP32 noetig | Neu (DRY-05 loeste nur "einmal je Image"); mittel / mittel |
| DRY-R08 | Niedrig | `kiss_functions.cpp:462-540` gegen `net_console.cpp:76-100, 148-215` | `bytesToHex`, `hexToBytes`, `ctEqual`, NONCE-HMAC-Challenge 2x; `hmac_sha256.cpp` hat `hexLower`/`ctEqual` schon | -50 Z., ca. -0.2 KB (authTask 722 B) | gemeinsamer Header `hmac_challenge.h` auf `hmac_sha256.h`-Basis | Mittel (Auth-Pfade). Wire fix: `NONCE: <32 hex>\r\n`, HMAC-SHA256-Hex | Neu; hoch / niedrig |
| DRY-R09 | Niedrig | `nbr_matrix.cpp:2840-2862` gegen `:2988-3010` | `nbrExclusive` / `nbrExclusiveDirect` unterscheiden sich in 1 Zeile | ELF 171+242 B, -22 Z., ca. -0.1 KB | gemeinsamer Helper mit Flag | Niedrig (NBR_LOCK-Umfang beibehalten) | Neu; hoch / mittel |
| DRY-R10 | Niedrig | `loop_functions.cpp:1722-1760` (Head, mit redundantem 2. ON-Block), `:1816-1836`, `:1926-1952`, `:3195-3222`; Batt/Status-String `:2024-2062` (Time), `:2095-2128` (Mainline), `:2200-2215` (wpRefreshClock) | Display-Off/On-Guard 4x; `cbatt/cstatus`-Bau 3x | -90 Z., ca. -0.2 KB (OLED/TFT) | `displayPrologue()`, `fmtBattStatus()` | Niedrig/Mittel (Feldbreiten `%4`/`%5`, T-Echo) | **Bekannt** (04-doc "pasted 3x", Zeilen veraltet); hoch / niedrig |
| DRY-R11 | Niedrig | `lora_setchip.cpp:300-358` gegen `:391-461` (nRF52-Zweig) | `SetRxConfig/SetTxConfig` 2x. **Drift:** der RadioLib-Zweig (`:469-492`) kennt US (SF7/CR6), PL (SF9/CR7) und `node_track_freq`. Der SX126x-Arduino-Zweig kennt nur TX-US, nie RX-US, kein PL, Frequenz fest `LORA_APRS_FREQUENCY` | -40 Z. | Profil-Funktion `aprsProfile(country)` plus ein Helper | **Verhalten (Funk)**, nur LoRa-APRS-Tracking auf RAK4631/T114/T-Echo; Betreiber entscheiden | Neu (Twin-Drift); mittel |
| DRY-R12 | Niedrig | `esp32/serial_command_esp32.cpp` gegen `nrf52/serial_command_nrf52.cpp` (ca. 110 von 144 Z. gleich), `sanitize_loaded_settings` in `esp32_flash.cpp:225` gegen `nrf52_flash.cpp:372` (ca. 25 Z. gemeinsam) | Twins | 0 Flash, ca. -140 Z. | `.inc` mit `#if`, nur diese beiden | Niedrig | **Bekannt** (C06, DRY-22); hoch |
| DRY-R13 | Niedrig | `gps_functions.cpp:75, 265, 294, ...` (28 identische `#if T114/T_ECHO -> Serial1 else GPSSerial`), DDMM-Umrechnung `aprs_functions.cpp:1185`, `loop_functions.cpp:4889`, `esp32_main.cpp:4732`, `nrf52_main.cpp:2141` | Alias fehlt; 4x Grad->DDMM | -60 Z., ca. 0 Flash | `#define GPS_UART`, `degToDdmm()` | Niedrig | Neu; hoch |
| DRY-R14 | Niedrig | `msgstore.cpp:424-428` / `:463-467` (Slot-Init 2x), `loop_functions.cpp:4761, 4859, 5476` (Fan-out BLE/EXTUDP/KISS 3x) | Kleinkram | je <0.1 KB | `msgstoreInitSlot()`, `fanoutOwnTx()` | Niedrig | teils bekannt (DRY-23) |

**Nebenbefund, kein DRY (Flash-Guard):** `src/bmp390.cpp:5` prueft `#if defined(ENABLE_BMX280)`, `bmp390.h` prueft `ENABLE_BMP390`. In T-ETH-ELITE_1262, t_deck, t_deck_plus und t_echo ist BMP390 abgeschaltet, `bmp390.cpp` wird aber mit dem Adafruit_BMP3XX-Objekt trotzdem uebersetzt. Ein Wort Fix, Ersparnis vermutlich <0.5 KB (gc-sections), Konfidenz niedrig. Nicht gemessen, da kein Build fuer diese Envs vorlag.

## 2. Top 5 (Nutzen/Risiko)

1. **DRY-R01** web_setup-Tabelle: der groesste Einzelposten, mechanisch, ESP32-Web-Envs. Vorher `producer_match_lint.py` anpassen oder die Rungen als Makro `WS_PARAM(...)` mit gleichem Muster halten.
2. **DRY-R02** commandAction-Helfer: niedriges Risiko, behebt nebenbei das UB bei leerem Argument.
3. **DRY-R05** Extern-UDP-Sendeblock: klein, risikoarm, Logformat bleibt.
4. **DRY-R06** Uhr-Sync-Helper: wirkt in ESP32 und nRF52 und beseitigt die Schwellen-Drift (2020/2023).
5. **DRY-R07** SHA-256 auf mbedtls (ESP32): ca. 1,3 KB x 29 Envs. Erst nach Bench der RM-Auth anfassen.

Schnellgewinn nebenher: der `bmp390.cpp`-Guard.

Finger weg (Risiko > Nutzen): DRY-R03 und DRY-R04 (Funk-RX/TX-Pfad, Reihenfolge ist Semantik), ausser byte-gleiche Paare.

## 3. Geprueft und verworfen

- `batt_function_old.cpp` gegen `batt_functions.cpp`: per `USE_NEW_BATT` exklusiv, gemeinsamer Kern in `batt_pipeline.h`, kein Env linkt beide (bekannt, DRY-20).
- Platforms/{VisionMasterE213,E290,WirelessPaper} (`power_controls.cpp`, `spi.cpp`): je ein Env, 0 Flash, bekannt.
- t-deck-pro gegen t5-epaper (peri_gps, ui, scr_mrg, ui_port) und `ui_deckpro` gegen `t-deck/lv_obj_functions`: getrennte Binaries, bekannt; teilweise schon nach `ui_common` gezogen.
- `nrf52/api_functions.cpp` (`lora_t114_init` gegen `lora_t_echo_init`): `#ifdef`-exklusiv.
- `loop_functions.cpp` "Save last Text" 4x (`:1401`, `:1497`, `:1544`, `:1626`) und `sendDisplay1306`-Zweige: stehen in exklusiven `#if`-Zweigen, 0 Flash.
- BLE-JSON-Builder (`sendBleJsonRegister`, SN/SN1/AN/SA): schon ein Helper, Daten sind Wire-Format.
- Toggles und Numerik-Setter im Ladder: schon tabellengetrieben (`command_toggles.h`, `command_setters.h`).
- Settings Load/Save: Deskriptor-Tabelle (`settings_store`), Backends unterscheiden sich im Storage.
- `esp32_flash.cpp` gegen `nrf52_flash.cpp`, `udp_frame_*`-Twins, `esp32_main` gegen `nrf52_main`: Twins, 0 Flash. Twin-Lints und Tests existieren, `udp_frame.h` begruendet das Nicht-Zusammenlegen.
- Sensor-Wrapper aht20/sht21/bmp390: je ca. 15 Zeilen Boilerplate, bibliotheksspezifisch, <100 B.
- `cround4` gegen `r4` in `pos_pipeline.cpp`: absichtliche Kopie (Kommentar, native-testbar).
- `clock.cpp` gegen `clock.h`: nur Lizenzkopf. Doppelte String-Literale: der Linker fuehrt sie zusammen.
- Web-Karten-Boilerplate in `web_functions.cpp` (13x vier `println`): ca. 150 B, Strings werden gemergt.
- `OnRxDone`-Epilog 5x (`rxBufInUse`, `is_receiving`, `csma_timeout`): <0.1 KB, Funk-kritisch, nicht lohnend.
