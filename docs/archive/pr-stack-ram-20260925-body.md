## Ueberblick

Acht Aenderungen aus dem Feld- und Bankbetrieb seit v4.35t.09.20, alle auf Basis von `upstream/dev` (8cff4395, inkl. KISS #1151 und Via-Settings #1155). Schwerpunkt ist ein reproduzierbarer Absturz auf ESP32 mit `--extudp on` (Stack-Ueberlauf im Loop-Task) und der RAM-Engpass auf dem klassischen ESP32. Nur `src/`, `variants/` und `platformio.ini`

## Was wurde geaendert

### 1. Stack-Ueberlauf im ESP32-Loop-Task (Extern-UDP)

- `platformio.ini` (`[esp32]`) und fuenf `variants/*/platformio.ini` (t5_epaper, t_deck_pro, vision-master-e213 inkl. -preview, vision-master-e290, wireless-paper): `-D ARDUINO_LOOP_STACK_SIZE=12288`. Die beiden `*-safeboot`-Envs bleiben aussen vor, `esp32-external-radio` erbt ueber `env:t_deck_pro`.
- `src/extudp_functions.cpp`, `sendExtern()`: `c_json[500]`/`c_tjson[500]` liegen jetzt auch auf ESP32 statisch (BSS) statt auf dem Stack, wie auf nRF52 schon seit 1951aa7d. Der `#ifdef ESP32`-Zweig entfaellt.

### 2. `--mesh off` auf Via-Pfaden

- `src/via_functions.cpp`, `checkMesh()`: der Via-Zweig liefert jetzt `bMESH` statt hart `true`. Neue Hilfsfunktion `pathNamesCall()` vergleicht den kommaseparierten Pfad Token fuer Token auf volle Laenge statt per `indexOf()`.

### 3. Leere Ziel-IP in der Extern-UDP-Bootzeile

- `src/extudp_functions.cpp`: `str_ip` wird im DNS-Zweig nach `hostByName()` aus der aufgeloesten Adresse gesetzt (nur ESP32, die nRF52-`IPAddress` hat kein `toString()`).

### 4. DHCP-Hostname aus dem Rufzeichen

- `src/configuration_global.h`: neue Funktion `makeDhcpHostname()` baut aus `meshcom_settings.node_call` ein RFC-1123-Label (nur `[A-Za-z0-9-]`, keine Bindestriche am Ende, Pufferlaenge beachtet) und liefert `false` fuer einen unkonfigurierten Knoten.
- `src/udp_functions.cpp`: `WiFi.setHostname()` unmittelbar vor jedem Wechsel in den STA-Modus.

### 5. Ping schlaegt laut statt still fehl

- `src/loop_functions.cpp`/`.h`: `sendPing()` liefert `PingResult` (`QUEUED`, `SUPPRESSED_TRACK`, `RING_REFUSED`) statt `void`. Beide Fehlerpfade geben einen Grund aus, auf eine Zeile je Episode begrenzt. Ein abgewiesener Ring-Eintrag kehrt vor `DisplayPong(SENT)` zurueck.
- `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`: die Aufrufer melden das Ergebnis statt den Versuch; TRACK-Unterdrueckung verbraucht keinen `node_pingcount` mehr.
- `src/command_functions.cpp`: `--pingcall`/`--ping start` warnen bei aktivem TRACK, `--track on` warnt bei gesetztem Ping-Ziel. `node_pingcall` wird mit der eigenen Groesse statt `sizeof(node_call)` begrenzt.

### 6. TX-Marker auf beiden Plattformen gleich

- `src/lora_functions.cpp`, `doTX()`: `[MC-DBG] RADIO_TX` an allen drei Sendestellen, je RAK/Nicht-RAK, ueber eine Hilfsfunktion, mit angehaengtem Feld `kind=track|aprs|msg`. Der bestehende Zeilenanfang bleibt gleich.
- nRF52: `CAD_FREE` jetzt vor `doTX()` wie auf ESP32, `TX_START` neu, `OnTxTimeout()` schliesst den Zustand mit `rc=-1`. Nur Ausgaben, kein geaendertes Sendeverhalten.

### 7. RAM: Byte-Ringe statt Slots und kleinere Changes

- Neu `src/byte_fifo.h`/`.cpp`: Byte-Ring, Frames dicht hintereinander (Laengenbyte plus Nutzlast), eigener Lock auf nRF52, Generationszaehler fuer Verdraengung.
- Die drei Ausgangsringe (BLE-Daten zum Telefon, BLE-Kommandos, UDP-Ausgang) sind darauf umgestellt: Schreiber (`addBLEOutBuffer()`, `addBLEComToOutBuffer()`, `addUdpOutBuffer()`), Telefon-Leser (`src/phone_commands.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`), Web-Nachrichtenseite (`src/web_functions/web_functions.cpp`, ueber einen Verlaufs-Iterator) und beide UDP-Drains (`src/udp_functions.cpp`, `src/nrf52/nrf52_main.cpp`, jeweils peek/tail_gen/pop). Die rund 25 Aufrufer von `addBLEOutBuffer()` bleiben unveraendert. Groessen je Klasse in `src/configuration_global.h` (`RING_BYTES_*`); der Kommando-Ring hat ueberall 3072 B, damit der ganze BLE-Config-Burst beim Connect hineinpasst.
- `src/mheard_functions.cpp`, neu `src/mheard_throttle.h`: die Drossel in `sendMheard()` vergleicht die ungelesenen Frames (`bf_unread()`, nach oben durch 256 B je Frame beschraenkt) mit der Kapazitaet.
- `src/web_functions/web_functions.cpp`: der statische 1-kB-Puffer `web_header_collect` entfaellt, `work_webpage()` sammelt direkt im String (gleiche Obergrenze, 1023 Zeichen).
- `src/loop_functions.cpp`, `src/loop_functions_extern.h`, neu `src/display_pages_cfg.h`: `pageLine`/`pageLastLine` als `int16_t`; `pageLastTextLong1/2` (2 x 675 B) nur noch auf Boards mit TFT oder E-Paper, wo sie beschrieben werden.

## Warum

1. **Stack-Ueberlauf:** Der arduino-esp32-Default fuer den Loop-Task ist 8192 B, und im Baum stand kein Override. Am kompilierten Heltec-V3-Artefakt gemessen (`-fstack-usage` plus die `entry`-Prologe der Xtensa-Windowed-ABI) braucht die Kette `esp32loop -> commandAction -> ... -> sendExtern -> decodeAPRS -> printfdeb -> MeshSerial/lwIP` 8128 B, ueber `sendMessage` 9248 B; der Extern-UDP-Eingang `getExternUDP -> getExtern -> sendMessage -> sendExtern` lag mit den beiden Puffern auf dem Stack bei 8464 B. Auf Xtensa laufen Interrupts auf dem Stack der unterbrochenen Task, die Zahlen sind untere Schranken. Auf einem Heltec V3 mit einer Variante unseres Forks, in der dieselbe Kette rund 1600 B tiefer liegt, setzte ein Extern-UDP-Datagramm `{"type":"msg"}` mit fremdem Ziel den Knoten deterministisch zurueck (4 von 4, Reset rund 120 ms danach). Auf diesem Stand liegt die Kette nach der Messung ebenfalls ueber 8192 B. Im Feld faellt es seltener auf, weil der lwIP-Anteil (rund 1950 B) nur dann auf der Kette liegt, wenn ein Client an der Netzkonsole haengt.
   Wichtig: der Name muss `ARDUINO_LOOP_STACK_SIZE` sein, nicht `CONFIG_ARDUINO_LOOP_STACK_SIZE`. Letzteres definiert die `sdkconfig.h` des Frameworks selbst auf 8192, sie wird nach der Kommandozeile eingebunden und gewinnt still. Am Artefakt geprueft: `getArduinoLoopTaskStackSize()` liefert 0x3000 statt 0x2000. Kosten: 4 kB Heap beim Start.
   Die statischen Puffer sind sicher: alle Aufrufer von `sendExtern()` laufen im Loop-Task, `OnRxDone()` geht nur ueber `queueExtern()`.
2. **`--mesh off`:** Seit dem Via-Routing (v4.35p) gab der Via-Zweig von `checkMesh()` hart `true` zurueck, sobald das eigene Rufzeichen im Pfad stand; `checkMesh()` ist die einzige Relay-Schranke, der Knoten relayte also trotz `--mesh off`. Dazu traf `indexOf()` jedes Rufzeichen, dessen Praefix das eigene ist (DK5EN-1 auf DK5EN-14, jedes Basisrufzeichen auf seine SSIDs). Zusammen relayte ein Knoten mit `--mesh off` fremd adressierte Frames.
3. **Boot-Zeile:** Mit Hostname statt IP in `node_extern` zeigte die Zeile "now sending to IP" keine Adresse, was bei der Fehlersuche irrefuehrt.
4. **DHCP-Hostname:** Ohne gesetzten Hostnamen erfindet der Core `esp32-XXXXXX` aus den MAC-Bytes; in der Lease-Liste des Routers ist der Knoten damit nicht zuzuordnen. Gesetzt wird vor jedem STA-Eintritt statt einmal beim Init, damit ein geaendertes Rufzeichen ohne Neustart ankommt.
5. **Ping:** `sendPing()` brach im TRACK-Modus und bei vollem Ring still ab. Der ESP32-Aufrufer druckte vorher "send Ping" und verbrauchte trotzdem einen `node_pingcount`; der Bediener sah das Ping-Budget schrumpfen, waehrend nichts in den TX-Ring kam, und bekam ein Intervall spaeter ein irrefuehrendes "[PONG]...fail".
6. **TX-Marker:** `RADIO_TX` stand nur an einer von drei Sendestellen und nur im Nicht-RAK-Zweig; auf dem RAK kam er nie, fuer TRACK/LoRa-APRS auch auf ESP32 nicht. `CAD_FREE` bedeutete auf nRF52 etwas anderes als auf ESP32, `TX_START` fehlte auf nRF52, und ein TX-Timeout liess den Zustand auf `TX_ACTIVE` haengen. Logauswertungen ueber die Flotte verglichen damit verschiedene Groessen.
7. **RAM:** `BLEtoPhoneBuff`, `BLEComToPhoneBuff` und `ringBufferUDPout` waren je N x 260 B grosse Schlitzfelder und die groessten BSS-Posten; die gemessene mittlere Frame-Laenge liegt bei 77 B. Auf dem klassischen ESP32 ist DRAM (`dram0_0_seg`) das Nadeloehr, E22_XML-DevKitC stand auf unserem Zweig schon 1008 B ueber der Grenze. Gemessen: E22_XML-DevKitC 125 588 B -> 115 108 B, ttgo_tbeam 116 452 B -> 105 972 B. Die Drossel in `sendMheard()` muss gegen die ungelesenen Frames pruefen: `bf_used()` zaehlt den gelesenen Verlauf mit und faellt nie, eine Drossel darauf steht nach dem ersten Ringumlauf dauerhaft und die MHeard-Liste erreicht das Telefon nie wieder. Der BLE-Config-Burst (vor #1155 12 Frames, rund 1970 B gemessen; SN1 kommt jetzt als ein kurzer Frame dazu) muss vollstaendig in den Kommando-Ring passen, sonst fehlen der App Node-Settings; daher 3072 B in allen Klassen.

## Getestet

- Alle 32 Release-Envs gebaut, 32/32 erfolgreich. `esp32-safeboot` scheitert im Sammellauf nach den Mainline-Envs mit einem Python-`TypeError` im Build-Skript; das passiert identisch auf unveraendertem `upstream/dev` (8cff4395) und ist unabhaengig von diesem PR, einzeln gebaut ist es gruen.
- Host-Tests (auf unserem Fork, nicht Teil dieses PR): Regressionsfaelle fuer `checkMesh()` (vor dem Fix drei rot, danach 13/13), den Byte-Ring (13 Faelle), die MHeard-Drossel (vor dem Fix rot) und `makeDhcpHostname()` (8 Faelle); Gesamtlauf auf dem Fork 919 von 919 Faellen gruen.
- Hardware:
  - Stack-Fix: auf der Fork-Variante mit der tieferen Kette Absturz reproduziert; mit beiden Massnahmen 9 h 36 min Nachtlauf ohne Neustart, Extern-UDP-Live-Probe mit 4668 B freiem Stack (Hochwassermarke).
  - Stack-Fix und Byte-Ringe zusammen auf RAK4631 (W5100S, `--gateway on`, UDP-Drain) und Heltec V3 geflasht und geprueft (unser Fork-Stand, nicht dieser PR-Zweig).
  - `RADIO_TX`: `kind=msg` auf RAK und Heltec, `kind=aprs` auf Heltec beobachtet; `kind=track` nicht beobachtet (kein Positionsframe im Testfenster).
  - Nicht auf Hardware geprueft: die MHeard-Drossel mit gekoppeltem Telefon, `--mesh off` auf Via-Pfaden, DHCP-Hostname, Ping-Meldungen, Boot-Zeile.

## Hinweise fuer den Reviewer

- Die Byte-Ringe sind der groesste Teil (rund 500 Zeilen). Die Aenderung bleibt hinter den bestehenden Hilfsfunktionen; wer sie getrennt reviewen will, findet sie in eigenen Commits.
- Das Laengenbyte des alten Schlitzformats entfaellt: `bf_peek()` liefert die Nutzlast ab Offset 0, alle Indizes dahinter sind in beiden UDP-Drains und den Telefon-Lesern um eins verschoben (`udpSnapshot+1+36` -> `+36`).
- `addBLEComToOutBuffer()` hatte bisher keine Sperre und bekommt jetzt eine.
- Unabhaengig davon gefunden, nicht in diesem PR: das Gateway-DM-ACK in `lora_functions.cpp` sendet ebenfalls ohne `bMESH`-Pruefung. Das ist eine eigene Emission des Knotens, keine Weiterleitung; ob `--mesh off` sie umfassen soll, ist eine Betriebsentscheidung.
- Die DHCP-Hostname-Aenderung deckt nur WLAN auf ESP32 ab, nicht Ethernet (nRF52/W5100S, ESP32 mit `HAS_ETHERNET`).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
