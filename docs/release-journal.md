# Release Notes -- MeshCom Firmware v4.40a

Firmware `4.40a`, `FLASH_VERSION 20261030`, `FLASH_STRUCT_VERSION 20260724`
(`src/configuration_global.h`).
Aeltere Eintraege bis einschliesslich 2026-03-22 stehen im Archiv
[`docs/archive/release_lora_trx.md`](archive/release_lora_trx.md).
Dieses Journal ist seit 2026-10-02 hier (`docs/release-journal.md`); das `release.md` im
Wurzelverzeichnis ist wieder die Datei von upstream und wird nicht angefasst.

---

## Release v4.40a.10.30 (Freitag, 2026-10-30)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 68 Assets (neu:
`vision-master-e290-otp.bin` und sein `.autoupdate`-Zwilling). `FLASH_VERSION` 20261030, `FLASH_STRUCT_VERSION`
20260724 unveraendert -- die Einstellungen bleiben erhalten. Safeboot v3 unveraendert.

Basis ist jetzt das offizielle `v4.40a.10.28` (upstream `dev` 10203967, 2026-10-28): es enthaelt die gesamte
Fork-Firmware bis `v4.40a.10.25` (#1238), die CI-`.autoupdate`-Assets (#1239) und #1240 (sp2ong). Der Fork-Unterschied
in Release-Images sind nur noch die drei offenen PRs #1243-#1245 und der `dev`-Kanal (`MC_AU_DEV_CHANNEL`).

Inhalt seit `v4.40a.10.25` (Firmware):

- HEY-UP-01 (`a3a8fc5f`, `34d44b64`, upstream #1243): ein Gateway haengt beim HEY-Upload seinen Call an den Pfad
  (`appendCallToPath()`), gepaart mit seiner Signal-Gruppe (`appendHeySignalReport()` liefert jetzt bool). Vorher
  ~44 % der HEY-Berichte am Server ohne Empfaenger (DK5EN/map#5). `MC_PATH_MAX_CHARS` 119 (was `decodeAPRS()`
  annimmt); ein Relay, dessen Call nicht mehr passt, verwirft mit Grund `pathfull` statt mit Komma-Rest.
- Merge `upstream/dev` 10203967 (`2135826b`): #1240 neues Env `vision-master-e290-otp` (OTP-Waveform fuer
  FPC-7519revb, Partial Refresh, Full Refresh alle `E290_FULL_EVERY` = 10) und Blaettern durch bis zu 9 Nachrichten
  auf beiden E290-Images. Web-Flasher: beide Images, Wahl ueber die Displaykabel-Beschriftung (PF-01).
- UDP-05 (`2b1a9dc4`, upstream #1244): `sendMessage()` spiegelt auch `ORIGIN_EXTUDP` in den Phone-Ring, damit
  EXTUDP-Sendungen im Web-Chat erscheinen (DK5EN/map#3). Die Gateway-"Server erreicht"-Quittung bleibt Web/Phone.
- WEB-AUTH-01 (`9ac4f45f`, Kommentare `fb8c19d4`, upstream #1245, Issue #1241): Login per Formular (Enter,
  Autofokus), Nonce + HMAC-SHA256 im Browser (`/?loginnonce`, `/?login=<nonce>.<mac>`), Session-Cookie `mcs`
  (10 Slots, 4 h Leerlauf) statt IP-Freigabe; `/?nodepassword=<pw>` antwortet 403 `plaintext`; 5 Fehlversuche/min
  sperren 60 s; `--webpwd` beendet alle Sessions. Skripte brauchen den Cookie-Ablauf (`Web-API_documentation.txt`).

### Was fuer dieses Release auf Hardware geprueft wurde

- Gate auf dem Release-Commit: `tools/regression.sh --stage 1,2` PASS (84 Unity-Envs, 3111/3111, Golden 48, pytest 1422).
- WEB-AUTH-01 auf DK5EN-1 (Heltec V3, USB) 19/19 HTTP-Pruefungen inkl. Sperre und Chrome (Enter, Autofokus,
  Fehlermeldung, Logout); DK5EN-98 (OTA, Operator-OK) 17/17 plus Chrome. Beide auf `9ac4f45f`.
- UDP-05 auf DK5EN-1 (INSTRUMENT-Build von `2b1a9dc4`, 2 dBm): EXTUDP-JSON an UDP 1799 erscheint einmal als eigene
  Nachricht im Web-Chat.
- HEY-UP-01 im Feld auf DK5EN-98 seit 2026-10-30 17:22: `DL2JA-2,DK5EN-98` im INTERLINK-Feed, Kante in mcmap
  gezeichnet, Publish-Rate unveraendert.

### Was ausdruecklich NICHT geprueft wurde

- E290-OTP-Image: kein E290 auf der Bench, nur gebaut; Displayverhalten ist der Test des upstream-Autors.
- HEY-Pfadgrenze 119 und Relay-`pathfull`: nur Host-Tests (`test_hey_report`), kein 119-Zeichen-Pfad on air.
- WEB-AUTH-01: kein TLS, Cookie-Diebstahl durch Mitlauschen und Klartext-Einstellungen nach dem Login bleiben;
  `tools/webflash.py` gegen einen Knoten mit Web-Passwort nicht geprueft.
- `prod`-Kanal-Installation aus dem offiziellen `v4.40a.10.28` nicht probiert.
- Idle-Heap-Gate auf DK5EN-92 uebersprungen: der T-Beam war am 2026-10-30 weder per USB noch im LAN erreichbar
  (Sweep 192.168.68.0/24: nur DK5EN-1 und DK5EN-98). Statisch kommen 672 B dazu (`g_sess` 480 B, `g_nonce` 192 B,
  `nm` auf dem lokalen `ttgo_tbeam`-ELF), die 3072-B-Grenze ist damit nicht beruehrt, aber nicht gemessen.
- Alle Boards ausser Heltec V3 und dem Gateway DK5EN-98 fuer die Aenderungen dieses Releases nur gebaut.

---

## Release v4.40a.10.25 (Sonntag, 2026-10-25)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.24.
`FLASH_VERSION` 20261025, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben erhalten
(die drei TD-23-Felder sind neue NVS-Keys nur auf T-Deck/T-Deck Plus/T-Deck Pro). Safeboot v3 unveraendert.

Inhalt seit `v4.40a.10.24` (Firmware: 9 Commits plus Merge von Fork-PR #1 und Cherry-Pick von upstream #1237, dazu der `FLASH_VERSION`-Stempel):

- TXP-01 (`556b997d`): `pnFrameIsReleasableGroupText()` gibt keinen Frame mit eigenem Call als Quelle mehr frei.
  Vorher gab die Server-Kopie eines anderen Gateways einen eigenen Gruppen- oder `*`-Text frei, bevor er einmal
  gesendet war (DK5EN-98-Logger 2026-10-24/25: 45 von 103 eigenen Gruppentexten nie auf LoRa; Bench 9/10). Der
  40-s-Gehoert-Check stoppt die Wiederholung wie bisher; Texte fuer einen KISS-Client (fremde Quelle) bleiben freigebbar.
- Upstream #1237 (`ce4407b3`, cherry-pick -x von `41a49fac`, rainerfritz, gemergt 2026-10-25): SN1 an die App mit
  MHOP, MTU, VOLT, NCON, KISS; Settings-Push nach `--netconsole`, `--kiss`, `--volt`/`--proz`, `--mtu`; `--maxv` sendet
  das Info-JSON; INA226-Keys nur mit `ENABLE_INA226`. Ein Konflikt (Kommentar an `LEDNF`), upstream-Text behalten.
- SNF-ACK-01 (`e95e5dd9`): ein Store-Knoten hielt und lieferte eine bereits quittierte DM erneut aus (Feldfall
  2026-10-25, DK5EN-98 -> DM3KS-12 `{450` via DB0SD-22): Ack vor der eigenen Kopie, Re-Ack der Zustellung leert den
  Slot, spaete Kopie oeffnet einen neuen. Jetzt merkt `msgstoreOnAck()` (Sender, Acker, NNN) 1 h in einem Ring mit
  32 Eintraegen (nur fuer Acker im Store-Set), `msgstoreStore()` verweigert dafuer einen neuen Slot; Zaehler
  `tomb=` im `MBOX`-Zeile und auf der Web-Seite. Nur S3 und nRF52 (`ENABLE_MSGSTORE`), klassische ESP32 nicht.
- TD-20 (`5b1e1da8`): `persisted_msgs` hielt jede empfangene Nachricht bis zum Reboot im internen RAM
  (Staging fuer `/messages.json`, dessen Datei-I/O nur unter `T_DECK_SPIFFS` existiert, nirgends definiert).
  Nach ~250 Nachrichten fielen 124-B-DMA-Allokationen aus und der IP-Pfad hing. Jetzt nur noch unter `T_DECK_SPIFFS`.
- TD-21 (`5b1e1da8`) und TD-21b (`ace935c6`): IP-Liveness-Watchdog fuer alle ESP32 im WiFi-Modus. Alle 30 s
  drei ICMP-Echos an `WiFi.gatewayIP()` (seit WIFI-HEAP-01 per raw-pcb ohne eigenen Task); scharf
  erst nach der ersten Antwort; 3 Fehlrunden -> Reconnect, +3 -> Netz-Reset, +10 -> Restart
  (`[WIFI];watchdog;ipdead;...`). TD-21b: eine haengende Runde ueber einen Gateway-Wechsel hinweg wird erkannt.
- WIFI-HEAP-01 (im Release-Gate gefunden): die esp_ping-Session hielt dauerhaft einen Task mit 3 KB Stack,
  Idle-Heap DK5EN-92 63884 B (-5448 B, Gate rot). Ersetzt durch einen raw-lwIP-ICMP-pcb im tcpip-Thread
  (`tcpip_try_callback`, kein Task, Loop blockiert nie); nur eine Antwort auf die laufende Runde zaehlt;
  `[WIFI];probe;armed` bei der ersten Antwort. Unabhaengiger Review: zwei Punkte uebernommen (Runden-Vergleich,
  hoechstens ein eingereihter Send).
- TD-22 (`bae97fd1`): T-Deck hoechstens 16 Nachrichten-Tabs (LRU-Verdraengung inaktiver Tabs), unter 40 KiB
  freiem internem RAM werden die aeltesten Blasen des am laengsten inaktiven Tabs verworfen (aktiver Tab behaelt 10);
  System-Texte auf 1024 Zeichen begrenzt.
- TD-23 (`ef95563f`, Fork-PR #1, Merge `b5af54b0`): T-Deck-Hintergrundlicht gespeichert (`node_bl_day` 16,
  `node_bl_night` 4, `node_bl_auto` aus), SYM+B 16/12/8/4/2/1, Tag/Nacht ueber den Sonnenstand an der eigenen
  Position (-6 Grad, 1 Grad Hysterese), ohne Uhr oder Position Tag-Stufe; `--bl`, Setup-Tab "Auto Day/Night".
- INSTRUMENT-only, Release-Builds unveraendert: `[INSTR-HEAP]`-Zeile, `--heaplog`, `--wifips` (`b55a5a6a`),
  `--wifips none` verweigert bei laufendem BT-Controller statt Panic (`9e4d8781`, WIFI-PS-01), `--heapeat`.

### Was fuer dieses Release auf Hardware geprueft wurde

- Gate auf dem Release-Commit: `tools/regression.sh --stage 1,2` PASS (82 Unity-Envs, 2951/2951, Golden 48, pytest 1410).
- Idle-Heap DK5EN-92 (T-Beam v1.2, Release-Image, ueber 2323, `--boot-timeout-s 400`): 68728 B gegen Baseline
  69332 B (-604 B) PASS. Gemessen auf dem raw-Probe-Build vor den zwei Review-Punkten (ein bool mehr).
  Mit der esp_ping-Session vorher 63884 B (FAIL).
- TXP-01 auf DK5EN-1 (Heltec V3) und DK5EN-90 (RAK4631) nach Gruppe 9999, gleicher Aufbau vor/nach dem Fix:
  ohne Fix 6 von 10 eigenen Texten vor dem ersten TX freigegeben, mit Fix 0 von 20; 20 von 20 gesendet, 19 genau einmal.
- TD-20 auf DK5EN-14 (T-Deck Plus, INSTRUMENT): 400 Nachrichten, Plateau bei 41,6 KB frei, `persisted_msgs` 0
  (vorher Haenger bei ~250).
- TD-21 auf DK5EN-14 mit dem finalen raw-Probe (INSTRUMENT): mit `--heapeat` auf 588 B frei, IP-Pfad tot, Heap
  freigegeben, Pfad bleibt tot, nach 3 Fehlrunden `ipdead;reconnect`, Ausfall 2 min 0 s, kein Reboot
  (`td21c_bench.summary`; esp_ping-Variante 2 min 1 s, vor TD-21 keine Erholung in 20 min).
- TD-22 auf DK5EN-14: 20 Gruppen reihum -> 16 Tabs, 146 Verdraengungen, 0 Pings verloren; 500 Nachrichten in
  5 Gruppen -> Boden 77-mal gegriffen, 41,0-61,9 KB frei, keine Fehlallokation.

### Was ausdruecklich NICHT geprueft wurde

- #1237 nicht mit McApp geprueft (nur Builds und Host-Suite). Idle-Heap nach #1237 nicht neu gemessen (nur
  Kommando-Handler und JSON-Keys auf dem Stack); DK5EN-92 traegt fuer den Auto-Update-Test 10.24.
- SNF-ACK-01 nur Host-Tests (`test_msgstore`, 9 neue `test_tomb_*`, jeder rot gegen den alten Code) und Builds,
  nicht on air.
- Idle-Heap nach SNF-ACK-01 nicht neu gemessen: der Mailbox-Code wird auf klassischen ESP32 (T-Beam) nicht
  gebaut; DK5EN-92 laeuft das Release-Image von HEAD.
- TD-23 nicht auf Hardware (UAT durch den Operator steht aus): nur Host-Tests (`native_daylight` 11 Faelle,
  Sanitize, NVS-Roundtrip) und Builds.
- TD-21 nur auf dem T-Deck Plus ausgehungert (T-Beam nur `probe;armed`); Stufen Netz-Reset und Restart nur im Host-Test. Bei `td21b_bench` fiel die
  Assoziation ganz weg; der alte Watchdog verband erst nach `WIFI_WD_GRACE_S` (180 s) neu, nicht verkuerzt.
- Kein 24-h-Soak nach TD-20/TD-22 (der Soak wurde am 2026-10-25 nach 6 min fuer den TXP-01-Bench abgebrochen),
  keine Sichtpruefung der Tabs am Geraet.
- TXP-01 nicht auf DK5EN-98 im Feld (laeuft noch den `3e07f894`-Gate-Build); TTL-Werte nicht nachjustiert.
- Alle Boards ausser Heltec V3, RAK4631, T-Deck Plus und T-Beam v1.2 fuer die Aenderungen dieses Releases nur gebaut.

---

## Release v4.40a.10.24 (Samstag, 2026-10-24)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.23.
`FLASH_VERSION` 20261024, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben erhalten.
Safeboot v3 unveraendert.

Inhalt seit `v4.40a.10.23` (Firmware: 5 Commits, dazu der `FLASH_VERSION`-Stempel):

- SP-Server-Host `sp.meshcom.pl` statt `spmeshcom.dynalias.com` (`62782dd5`, Wunsch des Server-Betreibers;
  gleiche IP 146.59.232.234, nRF52-Literal unveraendert). Upstream als #1236 am 2026-10-24 gemergt, per Merge
  `ff67ecd5` synchronisiert.
- AU-21 (`e6e74bc6`, `c100451a`): Heltec Wireless Tracker (S3 ohne PSRAM) belegte 25,6 KB internen Heap fuer
  den TFT-Sprite ab Boot, auch bei gespeichertem Display aus; der AU-TLS-Handshake lief leer (`tls -9984`).
  Sprite nur bei Display an, fuer jeden AU-Check/Download freigegeben (`FW_SHED_TFT`, "Update..." direkt
  gezeichnet), danach neu belegt; Token `lowheap_tls` (GUI-Zustand H, verbraucht einen Download-Versuch);
  `--info` mit `HEAP free/min/blk` (alle ESP32) und TFT-Sprite-Bytes.
- RM-Client-Stop (`56519299`): ein von einem Client (McApp ueber Extern-UDP/BLE, Seriell, Web) per
  `sendMessage()` gesendetes RM1-Kommando wurde seit `6d058a5f` (10.11) nie gestoppt -- Kopie 1 plus 3 Repeats
  und `RETRANSMIT_GIVEUP`, obwohl das Ziel in Sekunden antwortete (Feldlog DK5EN-98 -> DK5EN-1, 2026-10-24).
  `src/rm_client_cmd.h` merkt (dst, ctr, msg_id), die Antwort stoppt die Kopien unverifiziert (kein Schluessel):
  `[RM];stop;client;...`. Ein `--rm`-Kommando bleibt verify-only.
- TRK-03 (`37d7e3ac`): T-Beam-Erststart setzte seit V4.21 `0x0035` (TRACK statt GPS); jetzt
  `TBEAM_FIRST_BOOT_SSET 0x0055` (Volt, Pos, Button, GPS), GPS startet ohne Reboot. Keine Migration.

### Was fuer dieses Release auf Hardware geprueft wurde

- Gate auf dem Release-Commit: `tools/regression.sh --stage 1,2` PASS (81 Unity-Envs, 2891/2891, Golden 48, pytest 1352).
- AU-21 auf DK5EN-1 (Heltec V3, `c100451a`): AU-Check findet v4.40a.10.23 mit `[AU];alloc;end;fails;0`,
  `--info` zeigt die HEAP-Zeile, keine `[TFT]`-Zeilen (kein `HAS_TFT`).
- TRK-03 auf DK5EN-92 (T-Beam v1.2): nach `erase_flash` mit dem Image vor dem Fix bestaetigt (TRACK an,
  GPS aus); Konfiguration danach wiederhergestellt (111 von 114 Feldern gleich, nur GPS-Position abweichend).

### Was ausdruecklich NICHT geprueft wurde

- Idle-Heap-Schritt auf DK5EN-92 ausgelassen (Operator-Entscheidung); es gilt der Wert von 10.23 (70296 B).
- AU-21 nicht auf einem Wireless Tracker (keiner auf dem Bench): Sprite-Freigabe und Neubelegung nur
  Host-Tests und Build. Offen: S3-Heap-Untergrenze fuer TLS, BLE-Pause auf S3.
- RM-Client-Stop nicht on air zwischen zwei Knoten geprueft (nur Host-Tests, neue Suite `native_rm_client_cmd` 15/15, und Builds).
- TRK-03-Fix nicht mit einem zweiten Erase auf Hardware geprueft (Regressionstest rot auf 0x0035, gruen auf 0x0055).
- Die 24-h-Nacht mit TXP (seit 10.23) auf DK5EN-98 und DK5EN-14 ist beim Release noch nicht ausgewertet.
- Alle Boards ausser Heltec V3 und T-Beam v1.2 fuer die Aenderungen dieses Releases nur gebaut.

---

## Release v4.40a.10.23 (Freitag, 2026-10-23)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.22.
Getaggt am 2026-10-24 mit dem Datum 10.23 (Operator-Entscheidung, der fuer einen Fix-Release freigehaltene
Name aus 10.22); Auto Update vergleicht MM.DD, 10.23 ist neuer als 10.22. `FLASH_VERSION` 20261023,
`FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben erhalten. Safeboot v3 unveraendert.

Inhalt seit `v4.40a.10.22` (Firmware: 19 Commits, 41 Dateien, +2780/-630):

- TX-Pool (TXP, fork-only, `cee174b7`, `05a18b36`, `2ce8872e`, `1f5521d9`, `eda5edb8`): der LoRa-Sendepuffer
  ist ein Slot-Pool statt eines Rings; TTL je Prioritaet (Relays 300 s, Position 120 s, ACKs 90 s; HEY
  behaelt 180 s); `{CET}`-Baken aelter als 150 s (oder aus der Zukunft, oder ohne vertrauenswuerdige
  Uhr) werden am Gateway, in der Queue und vor dem Relay verworfen; HEY/Position/CET ersetzen ihren
  wartenden Zwilling; DM-Retries nutzen ihren Slot weiter; Web-Gauge "TX queue" zaehlt alle belegten Slots,
  ACK-Wartende umrandet.
- Call-Blocklist `--blockcall` (BLK-01, fork-only, `ecf00df0`, `1635ba56`): ersetzt die Blocklist des
  DM3KS-Builds (sku2atsc/MeshCom-Firmware); `--info`, Web Info/Setup, `RM1 blockcall`.
- AU-19 (`be1589e0`): NimBLE-Panic in der BLE-Pause des Update-Checks (klassischer ESP32), Build-Patch
  `tools/nimble_patch.py`. AU-20 (`dea4f82a` + Patch-Hunk): ein verwaister esp_timer je BLE-Pause.
- AU-18 (`9b43b4f0`): Heap-Pruefung vor TLS beruecksichtigt beide mbedTLS-Record-Puffer.
- WEB-RX-01 (`5fdd4b8d`, Issue #1229): empfangene Pakete werden geparkt, solange ein Web-Send die Loop blockiert.
- RX-DEAF-01 (`15d0627b`, `7d9b6d74`, ESP32) und RX-DEAF-02 (`53681dd1`, nRF52): verwaistes `is_receiving`
  wird geloescht.
- GWRX-01 (`257e5e2b`, `0281dce4`): Server-Textframes nehmen am NBR-Overhear-Cancel teil, Server-Frames zaehlen
  das Gateway als Provider.
- GWACK-01 (`c462d018`): beide ACK-Kopien eines Gateways mit derselben Id.
- UDP-04 (`6e6e7e40`): doppelte Server-Position belegt keinen zweiten Dedup-Slot (ESP32).
- BL-01 (`deea597f`): Tests fuer `--mesh off`, Mock `--delay-client` (keine Firmware-Wirkung).
- Upstream #1235 am 2026-10-23 gemergt und per Merge `4898cada` synchronisiert (kein Code-Unterschied).

### Was fuer dieses Release auf Hardware geprueft wurde

- Gate auf dem Release-Commit: `tools/regression.sh --stage 1,2` PASS (80 Unity-Envs, 2861/2861, Golden 48, pytest 1332); alle 32 `default_envs` lokal
  gebaut, 27 `.autoupdate` mit `--check` byte-genau zurueckgewandelt.
- Idle-Heap: DK5EN-92 (Release-Code von `3e07f894`, nur `FLASH_VERSION` noch 20261022), BLE an, Gateway an:
  70296 B gegen Baseline 69332 B (+964 B, Toleranz 3072 B), PASS.
- TXP auf DK5EN-1, DK5EN-90, DK5EN-92 (Gauge, ACK-Pfad, 3 Retries in einem Slot ESP32 + nRF52, CET-Gate und
  Supersede am Gateway mit Mock-Server und Airgap); BLK-01 auf DK5EN-1/-90/-92; AU-19/AU-20 100 BLE-Pausen
  ohne Panic; AU-18 drei Checks; WEB-RX-01 0 von 44 verloren; RX-DEAF-01 erzwungener Latch; GWACK-01 28/28.
- Gate-Build von `3e07f894` laeuft seit 2026-10-24 16:20 auf allen fuenf Knoten (DK5EN-1/-14/-90/-92/-98),
  24-h-Logger fuer DK5EN-98 und DK5EN-14 auf rpizero.

### Was ausdruecklich NICHT geprueft wurde

- Die 24-h-Nacht mit TXP auf DK5EN-98 ist beim Release noch nicht ausgewertet; die TTL-Werte koennen danach
  angepasst werden. Das Relay-seitige CET-Gate wurde on air nicht beobachtet (nur Host-Tests).
- BLK-01: Uebernahme des Fremd-Schluessels aus dem DM3KS-Build nicht auf Hardware.
- UDP-04 und RX-DEAF-02 nur Host-Tests; 1-h-gwflood-Replay nicht gelaufen.
- Alle Boards ausser Heltec V3, T-Beam v1.2, T-Deck Plus und RAK4631 nur gebaut.

---

## Release v4.40a.10.22 (Donnerstag, 2026-10-22)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.21.
Getaggt am 2026-10-23 mit dem Datum 10.22 (Operator-Entscheidung): so bleibt `v4.40a.10.23` fuer einen
Fix-Release am selben Tag frei; Auto Update vergleicht MM.DD, 10.22 ist neuer als 10.21. `FLASH_VERSION`
20261022, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben erhalten. Safeboot v3
unveraendert.

Inhalt seit `v4.40a.10.21` (Firmware: 5 Dateien, +12/-9):

- UDP-03 (`92d3adbf`, upstream #1233, Issue #1231): der OTHER-Zweig von `handleUdpFrame_esp32`/`_nrf52`
  laesst `last_upd_timer` und `hb_warn_logged` in Ruhe; ein beliebiges Datagramm am Gateway-Port haelt die
  Server-Warnung nicht mehr stumm (nur Diagnose, Recovery war nie verzoegert).
- Upstream #1234 (`f1d09f5d`): Zeile SP (Polen) in `GW_SERVERS`, INET `spmeshcom.dynalias.com`
  146.59.232.234, Hamnet-Seite OTHER_NET.
- Upstream #1215 (`a89a06da`): OE-Bezeichnung "MeshCom OE (Austria/WW)"; die dort neu gebauten
  Safeboot-Images wurden nicht uebernommen (`--ours`, Safeboot v3 bleibt). Mitgekommen:
  `src/idf_component.yml.orig` (ohne Wirkung).
- Upstream #1225: Reihenfolge von drei LilyGo-Eintraegen in `default_envs`; #1219 + #1223 netto null;
  #1224 = der Sendepuffer, den der Fork schon hatte.
- Nicht-Firmware: `tools/mc_console.py` (ein 2323-Login, Passwoerter aus `.env`), Mock-Timer-Fix,
  `test_gw_servers` pinnt SP, Konzeptpapiere (TX-Pool, Mesh-Evolution), `REVIEW.md`, PuTTY-Anleitung.
- Upstream: #1235 (Konsole, GPS-Filter, Board-LED, Via-Pruefung, Build-Aufraeumen) offen.

### Was fuer dieses Release auf Hardware geprueft wurde

- Idle-Heap-Gate auf T-Beam v1.2 (`DK5EN-92`, lokaler Release-Build des Tag-Commits, BLE an, Gateway an):
  STAT-Heap 69712 / 69568 / 69068 B, Idle 69068 B gegen Baseline 69332 B (-264 B), bestanden
  (`tools/bench/runs/harness/heap-idle-v4.40a.10.22.json`).
- UDP-03 auf Heltec V3 (`DK5EN-1`, 2026-10-22): 13 OTHER-Datagramme in 130 s Server-Stille: ohne Fix keine Warnung,
  mit Fix Stufe 1 nach 59 s, Stufe 2 nach 89 s (`tools/bench/runs/harness/udp03-bench-20261022.json`).

### Was ausdruecklich NICHT geprueft wurde

- Die neue SP-Zeile gegen den echten polnischen Server (nur Host-Test `test_gw_servers`).
- Das Release-Image selbst auf der Bench-Flotte (Stand bei Tag-Push; siehe RESUME).

---

## Release v4.40a.10.21 (Mittwoch, 2026-10-21)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.20.
Getaggt am 2026-10-19 mit dem Datum 10.21: 10.20 traegt schon das Datum des Folgetags, und Auto Update
vergleicht MM.DD, ein Tag 10.19 waere aelter. `FLASH_VERSION` 20261021, `FLASH_STRUCT_VERSION` 20260724
unveraendert -- die Einstellungen bleiben erhalten. Neu: Safeboot v3 (nur per USB-Webflasher; die App
verlangt weiter v2).

Inhalt seit `v4.40a.10.20` (alle drei Firmware-Commits, in den Release-Notes untergebracht):

- WF-01/02/04 (`3f446720`): `t_deck_pro` (und `esp32-external-radio`) baut mit
  `partitions-16MB-safeboot.csv` statt `default_16MB.csv`; `pages_flasher.py` `check_partition_layout()`
  verweigert ein Manifest, das Safeboot oder App ausserhalb der Partitionstabelle schreibt. Webflasher:
  `...AU env=` aus `--info` waehlt genau ein Board, DTR0/RTS0 nach dem Lauschen auf nativem S3.
- WF-03 (`5b606a44`, `e0cc524a`): empfangene Bytes sichtbar, Safeboot-/ROM-Erkennung, "Neustart in die App"
  mit Wiederaufnahme des Ports; ROM per BOOT-Taste braucht die RST-Taste (nur Seite, keine Firmware).
- WF-05 F4-1 (`f33ca458`): Marker `mc_board_id_t` (`src/mc_board_id.{h,cpp}`, "MCBOARD1", hwid, env, tag) in
  `.rodata_custom_desc` = Image-Offset 0x120 (ESP32), Konstante auf nRF52; `--id` gibt
  `MCBOARD:v1:<hwid lauf>:<hwid bau>:<env>:<tag>` aus; `MC_ENV_NAME` fuer jedes Release-Env;
  `pages_flasher.py` verweigert ESP32-Images ohne passenden Marker; `pages/flash/identity.js`.
- WF-05 F4-2 (`2e7960ff`): Safeboot v3 (`src/safeboot/safeboot_id.h`) liest beim Start den Marker aus ota_0,
  gibt ihn aus und beantwortet `--id`/`--info`; `SAFEBOOT_VERSION 3`. Webflasher: `romread.js` (ROM-Client
  ohne Stub: SYNC, SPI_ATTACH, READ_FLASH_SLOW), Knopf "Aus dem Flash lesen", `--id` mit `--info`.
- Bench-Fixes (`f56b1f80`): alter Safeboot-Rest auf Bridge-Ports, Knotenzeile bei reiner Flash-Identitaet.
- Nicht-Firmware: AU-16 im Feld bestaetigt (`058e6dc3`), Kampagnendoku
  `docs/campaign-webflasher-autodetect-20261019.md`, Defektkatalog, Rezept `/release-firmware`.
- Upstream: nichts Neues; Marker und `--id` sind als spaeterer PR geplant.

### Was fuer dieses Release auf Hardware geprueft wurde

- `--id` auf `DK5EN-14` (`46:46:t_deck_plus`), `DK5EN-1` (`43:43:heltec_wifi_lora_32_V3`), `DK5EN-92`
  (`12:4:ttgo_tbeam`) und `DK5EN-90` (`9:9:wiscore_rak4631`), Bench-Builds des Release-Codes ohne Tag-String.
- Safeboot v3 auf `DK5EN-92` (UART) und `DK5EN-14` (natives USB): Marker beim Start, `--id` und `--info`
  beantwortet, Einstellungen erhalten.
- Webflasher (lokale Kopie, Chrome auf macOS): Erkennung nennt das Build-Env auf allen vier Knoten, Alias 12
  wird `ttgo_tbeam`; "Aus dem Flash lesen" auf `DK5EN-14` und `DK5EN-92`, auch mit stummem Board im
  ROM-Download-Modus, Neustart bringt die App zurueck; Node-Harness 1,0-1,3 s auf drei ESP32. RAK: Erkennung
  ueber `--id`, kein Flash-Lesen. `DK5EN-14` ueber den gehaltenen Port geflasht (33 s, Einstellungen erhalten).
- Gate: `tools/regression.sh --stage 1,2` PASS auf dem Release-Commit: Unity 77 Envs, 2740/2740,
  Golden-Selftest 48 Befehle, pytest 1172, nRF52-Board-Links, Web-GUI- und Webflasher-Suiten (detect 39,
  identity 9, romread 12).

### Was ausdruecklich NICHT geprueft wurde

- Idle-Heap-Gate: `DK5EN-92` nicht erreichbar (nicht am USB, Konsolenpasswort fehlte im Release-Lauf). Die
  Aenderung legt 128 B Konstante ins Flash und ein Kommando an, keine Heap-Allokation.
- Windows (usbser.sys), Bridge-Board CP210x beim Neustart-Pfad, Safeboot-v3-Hinweis im Browser (nur Unit-Test
  und Harness), T-Deck Pro auf Hardware (nur Build der neuen Tabelle), Marker und Flash-Lesen auf einem 1W.
- Diese CI-Images selbst auf der Bench-Flotte.
- Kein Marker-Check in CI (nur `pages_flasher.py` prueft beim Veroeffentlichen).
- Weiter offen aus 10.20: RM-NVS-01-Daten, AU-14, LoRa-QRT auf der Bench.

## Release v4.40a.10.20 (Dienstag, 2026-10-20)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.19.
Getaggt am 2026-10-19 mit dem Datum des Folgetags (gleicher Tag wie 10.19, `.N`-Suffix ist fuer Auto Update
unsichtbar). `FLASH_VERSION` 20261020, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen
bleiben erhalten. Safeboot unveraendert seit 10.05.

Inhalt seit `v4.40a.10.19` (alle vier Firmware-Commits, in den Release-Notes untergebracht):

- WEB-WDT-01 W1 (`3b54c452`): `web_txbuf.h` mit begrenzter Sendeschleife (Abbruch nach 2 s ohne Fortschritt,
  15 s Seitenbudget, Sendefehler), 20 Host-Tests, Watchdog-Fuetterung je Datensatz in Path, MHeard und
  Nachbarmatrix, `tools/bench/web_stall_client.py`.
- WEB-WDT-01 W2 (`34974374`): gesamte Web-Ausgabe ueber den Sendepuffer (`CommonWebClient`-Overrides,
  `web_txbuf.cpp`); ESP32 sendet nicht blockierend, nRF52 hoechstens `availableForWrite()`;
  `[WEB];tx;abort;<grund>`; `tools/bench/web_throttle.sh`.
- WEB-STOP-01 + RM-NVS-01 (`1cfa8ca2`): `stopWebserver()` nur bei laufendem Server (Log-Flut "WEBServer
  stopped" ohne IP, T-Deck-Feldmeldung); NVS-Zeile in `--info` und Logzeilen bei fehlgeschlagenem Speichern
  der RM-Knotenliste (E22-Feldmeldung, nur Diagnose).
- AU-16 (`22b13194`): T-Beam 1W baut mit `qio` statt `dio` (Board-JSON `esp32-s3-wroom-1-n16r8.json`,
  Bootloader jetzt 15104 B wie bei den anderen S3-Boards); Laufzeit-Sperre auf jedem ESP32-S3 per
  `SPI_MEM_CTRL_REG(0)`: Uebergabe an Safeboot, `--update install/apply`, Auto-Handover und `--ota-update`
  verweigert mit `bootloader_dio`, kein Override; `--update status` mit `boot;qio|dio;spi0`; Web-GUI-Hinweis
  mit gesperrtem Knopf (`/austatus` Schluessel `sbwhy`). 1W-Besitzer muessen einmal den Webflasher laufen
  lassen (Anleitung `docs/troubleshooting-tbeam-1w-autoupdate.md`).
- Nicht-Firmware: Bench-Laeufe A-F und Kampagnendoku (`8a86f55a`, `docs/campaign-web-wdt-20261019.md`),
  Defektkatalog Abschnitte 2d/2e.
- Nicht-Firmware: AU-16-Kampagne und Anleitung (`c2a6d533`, `docs/campaign-au16-dio-bootloader.md`).
- Upstream: nichts Neues; alles dieses Releases nur im Fork (WEB-STOP-01 ist PR-Kandidat, der Fehler steckt
  auch in upstream/dev).

### Was fuer dieses Release auf Hardware geprueft wurde

- WEB-WDT-01 Lauf A: TASK_WDT ohne Fix auf `DK5EN-1` reproduziert (`LAST_LOOP_SECTION=web`, 2,5 s
  Antwortverzoegerung).
- Laeufe B-E mit Fix (Build von `34974374`): kein Neustart auf `DK5EN-1`, `DK5EN-92`, `DK5EN-14`, `DK5EN-90`
  bei 2,5 s und 1,2 s Verzoegerung; Abbruch `stall`/`budget` wie vorgesehen; RAK endet nach 2048 B ohne Haenger.
  Lauf F: alle 14 Seiten auf `DK5EN-1` vollstaendig, `/` 94 kB in 501 ms.
- Idle-Heap-Gate (Operator-Entscheidung: Lauf D statt eigenem Flash): `DK5EN-92` (T-Beam v1.2), Build von
  `34974374` ohne Tag-String, BLE an, 2323, 620 s: 67748 B gegen Basis 69332 B, -1584 B, innerhalb 3072 B
  (`tools/bench/runs/harness/webwdt_heap_dk5en92.json`).
- AU-16 auf `DK5EN-14` (T-Deck Plus, gleiche S3-N16R8-Familie wie der 1W), nur der Bootloader getauscht: DIO
  (1W-Bootloader 10.19) zeigt das Feldbild von OE5HWN-17 Zeile fuer Zeile; QIO: Safeboot nach 7 s im LAN,
  nach 198 s zurueck in der App. Guard-Build: QIO uebergibt unveraendert (`spi0 0x012c2008`), DIO ueberall
  gesperrt (`spi0 0x00ac2008`), NVS intakt.
- Gate: `tools/regression.sh --stage 1,2` PASS auf dem Release-Commit: Unity 76 Envs, 2723/2723,
  Golden-Selftest 48 Befehle, pytest 1157, nRF52-Board-Links, Web-GUI-Suiten.

### Was ausdruecklich NICHT geprueft wurde

- `1cfa8ca2` (WEB-STOP-01, NVS-Diagnose) auf Hardware: nur Host-Tests und Builds; kein Heap-Wert fuer diesen
  Commit.
- Chrome-Durchgang von Lauf F (nur Skript-Client).
- AU-16 auf einem echten T-Beam 1W: kein 1W auf der Bench; ob er den QIO-Bootloader aus dem Webflasher
  bootet, soll der erste Webflasher-Lauf bei OE5HWN oder DD7MH bestaetigen.
- `22b13194` (AU-16) nicht heap-gemessen.
- Diese CI-Images selbst auf der Bench-Flotte; der Operator flasht die Knoten spaeter selbst.
- Weiter offen aus 10.19: LoRa-QRT auf der Bench und `DK5EN-98`-Nachtaufzeichnung, LED-Feldpruefung, AU-14,
  AU-17-Downloadpfad.

## Release v4.40a.10.19 (Montag, 2026-10-19)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei 10.18.
Begonnen am 2026-10-18 abends, mit dem Datum des Folgetags getaggt, weil das Release bis dahin dauert.
`FLASH_VERSION` 20261019, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben
erhalten, ausser den zehn gesperrten Protokoll-Einstellungen, die ein Release-Build beim Boot auf den festen
Wert setzt. Safeboot unveraendert seit 10.05.

Inhalt seit `v4.40a.10.18` (alle zehn Firmware-Commits, in den Release-Notes untergebracht):

- AU-Web-GUI W1 (`581f8a0a`): Zustandsknopf in der Auto-update-Zeile, `GET /austatus`, `notify` als
  Standard, automatische Installation nicht kompiliert (D14), X-MC-Gate fuer Update-Kommandos.
- AU-Web-GUI W2/W3 (`5a5fc7f7`): F-Ablauf gehaertet (Knoten ohne `/austatus`, veralteter Klick, Safeboot
  ohne Rueckfall), INSTRUMENT-Heap-Logging.
- AU-Web-GUI W4 (`64cd365d`): BLE-Guard (nur klassischer ESP32), Abwurf von KISS/Net-Konsole bei knappem Heap,
  `updcheck?ble`.
- Haertung W1 (`b8aa4354`): RM `sens` meldet nur erkannte Sensoren (BME280-Feuchte), LED-Helfer,
  `tools/bench/heap_idle.py`.
- Haertung W2 (`55ad3b42`): LED-Zeilen in `--info`, `boardLedTick()`, RM `chgled` lesen, `err pinbusy`,
  OneWire-Pruefung zur Laufzeit, Web-Schalter.
- Haertung W3 (`73c0bd04`): Telefon-Ringe nur bei laufendem BLE (T-Beam statisch 107688 -> 102584 B),
  `byte_fifo` mit portMUX, Update-Task-Stack 10240 B, `[HEAP]`-Bootmarken; dazu interner Gateway-Zustand.
- Haertung W4 (`e4edffc8`): Produktionssperre (`src/prod_lock.h`), `--autoupdate auto` in allen Builds
  abgelehnt, Setter-Fehler (`--postime`, `--ptime`, `--pingtime`, `--pingmax`), LED auf nRF52 und
  Varianten-Pins.
- LoRa-QRT W1 (`fe3f01e2`): `--nbrrelay on` fest, Echo gibt eigenen Gruppentext frei, kein Retry in QRT,
  vom Server gespiegelte eigene Frames nicht erneut auf LoRa, Ring voll -> QTA in jeder Tiefe, QRT-Ende unter
  16 mit einem QRV, Queue-Karte im Web.
- LoRa-QRT W2 (`1a6a3704`): altersabhaengige Haltezeit nach empfangenen Frames (20 s kurz, 60 s schnell),
  `cad=` ehrlich, `[MC-HWM]` mit `aged1`/`aged2`/`wait_hwm`.
- `cc48f720`: Watchdog wird beim Rendern der Nachbarmatrix je Zeile gefuettert (Feld-Neustarts DB0TAW-14).
- Nicht-Firmware: Doku der drei Kampagnen, Fork-vs-Upstream-Matrix, Spezifikation der gesperrten
  Einstellungen, `tools/heap_baseline.json` und der Idle-Heap-Schritt im Release-Rezept.
- Upstream: #1213 und #1214 am 2026-10-18 gemergt und zurueckgefuehrt (`e87e8a5e`); alles Neue dieses
  Releases nur im Fork.

### Was fuer dieses Release auf Hardware geprueft wurde

- Haertung W5 auf `DK5EN-92` (T-Beam v1.2, Release-Image): Idle-STAT-Heap 69332 B (unveraendert), Heap beim
  Update-Check mit pausiertem BLE 86276-86576 B (vorher 76,3-81,5 KB), Gate bestanden; AU-Task-Stack
  3740 B frei von 10240.
- Sperre: `[ERR];locked;<name>` auf `DK5EN-1` (nbrrelay, autoupdate auto, stor) und `DK5EN-90` (nbrrelay,
  autoupdate auto); `DK5EN-98` nach dem Flash: `--nbrrelay off` abgelehnt, NBRRELAY an, Gateway verbunden.
- LED: `--board led on` und `--info`-Zeilen auf `DK5EN-1`, `DK5EN-90`, `DK5EN-92` (OneWire auf GPIO4 sperrt);
  `DK5EN-14` zeigt beide LEDs "not fitted".
- AU-Web-Zeile in Chrome auf `DK5EN-1`, `DK5EN-14`, `DK5EN-92`: alle Zustaende, Install now -> Safeboot ->
  neuer Tag; BLE-Guard auf `DK5EN-92` mit BLE-Client-Skript, 20 Guard-Zyklen ohne Reset; `DK5EN-90` ohne
  AU-Zeile.
- Idle-Heap-Gate: `DK5EN-92` (T-Beam v1.2) auf lokalem Release-Build dieses Commits (`MC_BUILD_TAG` gesetzt), BLE an, kein Telefon, 2323: Idle-STAT-Heap 67304 B nach 600 s, Basis 69332 B, -2028 B, innerhalb 3072 B (Proben 69724 / 68128 / 67304 B; `tools/bench/runs/harness/heap-idle-v4.40a.10.19.json`).
- Gate: `tools/regression.sh --stage 1,2` PASS auf `cc48f720` + FLASH_VERSION: Unity 75 Envs, 2693/2693, Golden-Selftest 48 Befehle, pytest 1136, nRF52-Board-Links, Web-GUI-Suiten.

### Was ausdruecklich NICHT geprueft wurde

- LoRa-QRT W1/W2 auf der Bench (vom Operator gestrichen); Differential-Bench nicht gelaufen. Flotte und
  `DK5EN-98` laufen seit 2026-10-18 23:25 auf dem QRT-Stand, keine Aufzeichnung ausgewertet.
- `cc48f720` (Watchdog Nachbarmatrix) nur Host und Build.
- LED sichtbar blinken, Deep-Sleep-LED, RM `led`/`chgled` ueber LoRa, Telefon-App-SN auf `DK5EN-14`;
  LED-Pins T-Echo, T114, T-ETH-Elite, Heltec V4, S3 DevKitC im Feld.
- BLE-Guard mit McApp selbst; AU-Download auf dem T-Beam nach der Heap-Arbeit; Web-Fixes 2 und 3 der
  AU-Kampagne (nur Host).
- Diese CI-Images selbst auf der Bench-Flotte (der Idle-Heap-Schritt lief auf einem lokalen Build desselben Commits).

## Release v4.40a.10.18 (Sonntag, 2026-10-18)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`, Actions auf Node-24-Majors,
seit `ci-proof-node24-20261017` bewiesen), 66 Assets wie bei 10.17. Getaggt am 2026-10-17 abends mit
dem Datum des Folgetags (gleicher Tag wie 10.17, `.N`-Suffix ist fuer Auto Update unsichtbar).
`FLASH_VERSION` 20261018, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben
erhalten. Safeboot unveraendert seit 10.05.

Inhalt seit `v4.40a.10.17` (alle fuenf Firmware-Commits, in den Release-Notes untergebracht):

- GPS-11 W1 (`4221b16c`): `src/pos_filter.{h,cpp}` (HDOP-gewichtetes Positionsfilter, CV+ZUPT bei
  `--track on`, 30-min-Mittel mit Bewegungserkennung bei `--track off`), `src/pos_grid_hold.{h,cpp}`
  (0,3-Zellen-Hysterese im Stillstand).
- GPS-11 W2 (`a2381b97`): `src/pos_pipeline.{h,cpp}` in `WZ_GPS_Loop()`; `--gpsfilter on|off` (nur zur
  Laufzeit, Standard an), Debug-Zeile bei `--gpsdebug on`. Verhaltensaenderung auf der Luft: GPS-Boards
  (ESP32, ausser T-Deck Pro) senden die gefilterte Position. RAK4631 ohne Filter (W2b gestrichen).
- Net-Konsole (`b06a6907`): P-0111 Start im reinen AP-Modus, P-0169 `--netconsole off` schliesst den
  Listener, P-0168 `accept()` auch bei laufender Sitzung, Uebernahme nur nach gueltigem HMAC-Login,
  TCP-Keepalive 60/10/3.
- RM1 `net`/`upd` (`5c1d12b5`): `net` (MTU, Server, Netz), `upd`, `upd check`, `upd go <tag>`; `rs=` in
  `status`; Remote-Webseite mit Check und Update.
- `4d0d1756`: `ck=` bei nicht gestellter Systemuhr (GPS-Uhr) richtig, `fwCheckAgeMin()`.
- Nicht-Firmware: `fac759ed` (CI-Actions Node 24, `ci-build.yml` entfernt).
- Upstream-PRs: #1213 und #1214 weiter offen; GPS-11, Net-Konsole, RM1 `net`/`upd` nur im Fork.

### Was fuer dieses Release auf Hardware geprueft wurde

- GPS-11 auf `DK5EN-1` (Heltec V3, externer u-blox, Innenraum mit starkem Mehrwege-Empfang, Build
  `a2381b97`): 2 h `--track off` und 2 h `--track on` stationaer, kein Neustart, kein verworfener Wert.
  Gesendete Zelle 11 Wechsel in 2 h statt 119 roh; ein Fehlalarm Stillstand -> Bewegung durch einen
  30-s-Mehrwegesprung (40-m-Radius bleibt, Entscheidung des Operators).
- Net-Konsole auf `DK5EN-1`: P-0168 und P-0169 vorher/nachher
  (`tools/bench/runs/harness/p0168-*.summary`, `p0169-*.summary`).
- RM1 ueber LoRa `DK5EN-98` -> `DK5EN-1` (2 dBm): `status` `rs=0/1`, `net`, `upd`, `upd check`, `upd go`
  veraltet und aktuell; echtes `upd go v4.40a.10.17` aus bereitgestelltem Image -> Safeboot -> Release.
- VIA-01 auf `DK5EN-1`: USB-Konsole und Web-`setvia`, alle Faelle bestanden
  (`tools/bench/runs/harness/via01-heltec1.summary`).
- Gate: `tools/regression.sh --stage 1,2` PASS: Unity 72 Envs, 2521/2521, Golden-Selftest 48 Befehle, pytest 1100, nRF52-Board-Links.

### Was ausdruecklich NICHT geprueft wurde

- GPS-11 in Fahrt (G3, Smart-Beacon-Schuesse), auf T-Beam, T-Beam Supreme, T-Deck und den anderen
  GPS-Boards.
- Net-Konsole im reinen AP-Modus (P-0111) und Keepalive gegen halb offene Gegenstelle.
- RM1 `upd go` mit Download (`installing`), nur Host-Tests.
- Diese CI-Images selbst auf der Bench-Flotte (nicht geflasht).

## Release v4.40a.10.17 (Samstag, 2026-10-17)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei
10.16. Basis unveraendert das offizielle `v4.40a.hbserver`; `upstream/dev` ist vollstaendig in
`fork-dev` enthalten. `FLASH_VERSION` 20261017, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die
Einstellungen bleiben erhalten. Safeboot unveraendert seit 10.05.

Inhalt seit `v4.40a.10.16` (alle sechs Firmware-Commits, in den Release-Notes untergebracht):

- AU-13 (`c9e64344`): mbedTLS holt Speicher zuerst aus dem PSRAM (`mbedtls_platform_set_calloc_free()`,
  nur `BOARD_HAS_PSRAM` mit `psramFound()`), interner Rueckfall. `heapGateOk()` und `[AU];heap` messen
  internes RAM; PSRAM-Boards brauchen 17000 B PSRAM-Block und 24000 B intern frei. Ursache des
  Feldfehlers OE5HWN-16 (`[AU];tls;-32512`) und des Bench-Fehlers auf DK5EN-14 (`tls -1`, WLAN-Abbruch).
- VIA-01 (`be7c3c7f`, `9b96d884`): `--via` prueft die Liste mit `checkViaList()` vor dem Speichern
  (Rufzeichen, SSID 0..99 ohne fuehrende Null, keine leeren Token, kein `*`/`H`/`HG`, max. 39 Zeichen);
  `viaCanonical()` definiert die gespeicherte Form, Web-`setvia` meldet bei Kleinschreibung und `none`
  kein FAIL mehr.
- TD-21 (`0146fa0b`): Antwort-Knopf auf jeder eingehenden Blase der T-Deck, "To" wird aus dem
  Konversations-Tab vorbelegt. Verhaltensaenderung: aus einem Rufzeichen- oder Gruppen-Tab geht die
  Nachricht an diesen Tab statt an `*`.
- Web-Firmware-Spalte (`c7fe948c`): Firmware je Station auf der Path- und MHeard-Seite aus jedem
  dekodierten Frame; +2 B RAM je Zeile.
- `6d833289`: doppelte Pruefung von `msg_source_fw_on_air` vor `nbrNoteFw()` entfernt (gleiches Verhalten).
- Upstream-PRs: #1213 und #1214 (TD-21 + Firmware-Spalte) offen; AU-13 und VIA-01 nur im Fork.

### Was fuer dieses Release auf Hardware geprueft wurde

- AU-13 auf `DK5EN-14` (T-Deck Plus): 10/10 Dev-Checks direkt nach dem Neustart, keine
  Allokationsfehler, kein WLAN-Abbruch, internes Minimum 55,7 KB (vorher 2,7 KB); `--update install`
  hat 1,57 MB in 22 s geladen und bereitgestellt. Kontrolllaeufe `DK5EN-92` (T-Beam v1.2, klassisch,
  internes Minimum 11,6 KB beim Download) und `DK5EN-1` (Heltec V3, 25,3 KB) bestanden.
- TD-21 auf `DK5EN-14`: Antwort-Knopf und Vorbelegung vom Operator von Hand geprueft.
- Gate: `tools/regression.sh --stage 1,2` PASS: Unity 72 Envs, 2440/2440, Golden-Selftest 48 Befehle, pytest 1080, nRF52-Board-Links.

### Was ausdruecklich NICHT geprueft wurde

- VIA-01 auf Hardware (`--via dk8vw-p` gibt `[ERR]`, `--via none`, leer, `on`/`off`, Web-`setvia`).
- Web-Firmware-Spalte auf Hardware (nur Host-Tests).
- AU-13 im Feld beim meldenden T-Deck Plus (OE5HWN-16); T-Beam Supreme (PSRAM vom Build nicht aktiviert).
- Ein Auto-Update-Install mit anschliessendem Safeboot-Apply auf diesem Stand.

---

## Release v4.40a.10.16 (Freitag, 2026-10-16)

Von GitHub CI gebaut und veroeffentlicht (Tag-Push, Workflow `MeshCom CI`), 66 Assets wie bei
10.11. Basis ist jetzt das offizielle `v4.40a.hbserver` (icssw-org, 2026-10-13, upstream #1209-#1212),
vollstaendig in `fork-dev` enthalten. `FLASH_VERSION` 20261016, `FLASH_STRUCT_VERSION` 20260724
unveraendert -- die Einstellungen bleiben erhalten. Safeboot unveraendert seit 10.05.

Inhalt seit `v4.40a.10.11` (Firmware-Commits der ersten Elternlinie, alle in den Release-Notes
untergebracht; die uebrigen Commits im Bereich sind die upstream-Seite unserer bereits enthaltenen
PRs #1207/#1208):

- Zentrale Server-Tabelle `src/gw_servers.{h,cpp}` (`29c09ac8`, `cf66ad18`): ESP32, nRF52 dynamisch und
  nRF52 statisch waehlen Server und Zeitserver aus `GW_SERVERS[]` (OE, DL, IT, HC, HB; FR/US
  auskommentiert). DL im Internet nur `meshcom.hamnet.network`, DL im Hamnet ueber HC; HC im Internet
  ueber DL; IT im Hamnet bleibt auf dem Internet-Server. nRF52 waehlt DL/HC wie ESP32 (`8d232bde`),
  statische nRF52 bekommen HC und HB. Golden-Vergleich alt gegen neu ueber 700 Rasterzeilen.
- Netzmodus `node_honly` 0/1/2/3 und `--setnet auto|inet|hamnet`; auto nur fuer neue oder geloeschte
  Knoten; Aliase `--setinet`/`--sethamnet` bleiben; JSON-Bereich 0..3.
- ETH-05 (`cf66ad18`, `70f2c476`): statische RAK4631 bleiben bei der Ethernet-Wiederherstellung
  statisch; Hardware-Reset wartet auf den PHY, Wiederholung auch fuer statische Knoten.
- Konsole: UART-TX-Ring 2048 B auf allen ESP32-Boards mit UART-Konsole (`e0d5b1c9`, `8a1b2564`);
  STAT-Zeile mit OnRxDone avg/p95/p99/max (`c7cb2c1d`); INSTRUMENT-Treiber (`2a1dcdf3`, nicht in
  Release-Bildern aktiv).
- Upstream: Merge `v4.40a.hbserver` (`a880a029`, `39642f0a`); temporaerer OE-Fix `389ace0d`
  zurueckgenommen (`a748948d`).
- Upstream-PR #1213 (Server-Tabelle, Netzmodus, ETH-05) offen, ready for review.

### Was fuer dieses Release auf Hardware geprueft wurde

- Netzmatrix (`tools/bench/gw_net_matrix.py`, `docs/campaign-gw-net-matrix.md`) auf `DK5EN-90`
  (RAK4631, Ethernet) und `DK5EN-1` (Heltec V3, WLAN), je 45 Faelle: 3 Netze x 3 Modi x 5 Server.
  Serverwahl in allen 90 Faellen richtig; KEEP, BEAT vom Server und NTP in allen Faellen mit
  Rueckweg. Mit rpizero als Gateway zaehlt nur Hamnet (rpizero macht NAT nur zum ppp0).
- ETH-05 auf `DK5EN-90` mit INSTRUMENT-Build: `--ethdrop` setzt die statische Konfiguration ohne
  DHCP neu, zweiter Abbruch mit Hardware-Reset in 1,9 s zurueck, innerhalb 5 min kein zweiter Reset.
- Nach dem Lauf: `DK5EN-1` als Gateway (OE, BEAT) und `DK5EN-90` ohne Gateway auf diesem Stand mit DHCP.
- Gate: `tools/regression.sh --stage 1,2` PASS: Unity 72 Envs, 2419/2419, Golden-Selftest 48 Befehle, pytest 1080.

### Was ausdruecklich NICHT geprueft wurde

- Kabelziehen und ein echter UDP-Sendefehler-Streak auf dem RAK4631 (ETH-05).
- ESP32-Boards mit Ethernet (T-ETH-Elite), T114, T-Echo mit der neuen Tabelle.
- Gateway-Betrieb ueber Wochen; Downgrade mit gespeichertem Modus 2/3.
- Internet-Server ueber rpizero als Gateway (kein Rueckweg, Aufbau-bedingt).

---

## Release v4.40a.10.11 (Sonntag, 2026-10-11)

Erstes Release, das von GitHub CI gebaut und veroeffentlicht wird: der Workflow `MeshCom CI` ist auf
dem Fork wieder aktiv und startet auf Tag-Push. 66 Assets = 39 (27 ESP32-Anwendungsbilder inkl.
`T-ETH-ELITE_1262`, 3 nRF52 als `.uf2` plus DFU-`.zip`, 6 Hilfsdateien) plus 27 `.autoupdate`
(zlib-Zwillinge fuer Auto Update). Der Web-Flasher liefert die Anwendungsbilder dieses Releases.
`FLASH_VERSION` 20261011, `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben
erhalten. Safeboot unveraendert gegenueber 10.05/10.06.

Einordnung der Tags: `v4.40a.10.05` und `v4.40a.10.06` existieren als Tags, ihre Release-Objekte sind
auf GitHub nicht mehr vorhanden (`gh release list` zeigt nur `v4.40a.10.02`; 10.05 und 10.06 waren am
2026-10-05 veroeffentlicht, laut `docs/RESUME.md`). Dieses Release loest beide ab; `release-notes.md`
beschreibt das gesamte Delta gegen das offizielle `v4.40a` und listet das Neue seit dem Tag
`v4.40a.10.06` zuerst. Kanaele von Auto Update: `prod` liest die Releases von icssw-org, `dev`
(`--updchan dev`) das Latest-Release dieses Forks.

Inhalt seit `v4.40a.10.06` (71 Commits in `src variants platformio.ini`, alle in den Release-Notes
untergebracht):

- RM erweitert (#1189): neun neue Befehle (radio, name, atxt, pos, sens, mh, txq, mbox, maxhop), Ergebnis
  108 / Argumente 39 Byte, Antwort-Sanitiser, `p=<cur>/<min>/<max>`, Sender begrenzt `txpower` pro Ziel,
  gueltiger Tag + gesperrter Befehl wird beantwortet ohne Strike, `sync` mit eigenem 60-s-Limit,
  RM-PROOF (Budget 2 / 10), RM1 als Send-once-Text mit PN-Leiter bis zur geprueften Antwort, RM-GWRELAY
  (Befehle ueber den Serverpfad), Zaehler und Sperre pro Rufzeichen-SSID, RM-DUP (zweite Kopie eines
  Rahmens wird vor der Pruefung verworfen), Kompatibilitaetsschicht entfernt, eine Befehls- und eine
  Meldungstabelle (Review-Wellen 1-3). RAK-`radio` rechnet die Einheiten um.
- Web-GUI: Remote-Seite (Knotenkacheln, Schluesselspeicher, drei Slots, Heard-Tabelle, TX-Queue- und
  Mailbox-Balken, fuenf Karten, Aktionskacheln), Identity- und Position-Karte, TX-Leistung als
  Zahlenfeld, Anfrage-Wache `MC_WEB_GUARD` (Standard aus, nur RM-Endpunkte an), Status-Token,
  "Server:"-Zeile auf der Infoseite.
- LEDs: Lade-LED folgt dem Laden (CHG-01..04, Marker `node_sset2` Bit 0x8000 statt `node_sset3`,
  `--charge led on|off`, BLE-Export `CHGLED`, Init auch auf AXP2101), Board-LED-Blinken, Polaritaet
  per Makro, RM `led on|off|flash` und `chgled auto|on|off`, Web-Schalter, Heltec V4 mit Fern-LED
  (GPIO35, gebaut, ungetestet). `--gateway pos` und `--netconsole off` loeschen Bit 15 nicht mehr.
- Upstream-Uebernahme: elf Cherry-Picks (#1193 RadioLib-Flags, #1195 LED-Makro, #1196 Lade-LED,
  #1202 Board-LED-Blinken samt Folge-Commits); #1200/#1201 (Temperatur-Offset) waren schon im Fork.
  Tote Hemisphaeren-Umkehr in `tdeck_add_to_pos_view` entfernt.
- Sensoren/Eingaben: Temperatur-Offsets fuer AHT20, SHT21, BME680; Web-Offset akzeptiert fuehrende
  Null und Dezimalkomma; `--bme|--bmp|--bmx off` loescht nur das BMP390-Bit in `node_sset3`.
- HEY-Echo-Upload (eigene HEY-Echos, wie empfangen, ohne eigenen Rapport); S&F speichert keine DMs
  von/an Dienstrufzeichen und erkennt die `WLNK-1`-Quittung.
- ETH-04 (RAK4631, DB0AIS): Lease bleibt bei der Wiederherstellung, W5100S-Reset nach zwei
  DHCP-Fehlern mit Link, RNG aus dem SoftDevice, Entscheidungslogik mit nativen Tests.
- Auto Update: Asset heisst `<Board>.autoupdate` statt `.bin.zz`; versioniertes Safeboot sperrt Auto
  Update, solange es zu alt ist (AU-12); Safeboot meldet den echten `Update.end()`-Fehler, prueft die
  App nach dem Upload erneut, `/ota/state` mit Heap-Werten; AU-D13: Heap-Gate vor TLS auf dem
  klassischen ESP32 (76 KB Gesamt-Frei), Telefon an BLE gibt sauber `lowheap` statt -9984/-10368 mit
  Heap bis 268 B.
- Bauen: `-Os` auf t_echo und heltec_t114, nRF52-Linkschritt im Gate.

### Was fuer dieses Release auf Hardware geprueft wurde

- Host: 2371 Testfaelle in 71 nativen Umgebungen gruen, Golden-Selftest, pytest- und
  Tool-Suiten.
- DK5EN-92 (T-Beam v1.2, klassisch, AXP192): Lade-LED folgt dem Laden nach dem Update, `--charge led
off|on` mit Neustarts, RM `chgled on|off|auto` von DK5EN-1 (`chgled on` danach Neustart: Override
  weg), `led` antwortet `err unsupported`; Auto-Update-CHECK mit und ohne Telefon an BLE (mit Telefon:
  `lowheap`-Ablehnung, Minimum 58 KB; ohne: CHECK ok).
- DK5EN-1 (Heltec V3): RM `led on`, `led flash`, `led off` (dauerhaft, kurzes Blitzen alle 2 s,
  dunkel), `chgled` antwortet `err unsupported`.
- Frueher gebencht, Codepfade unveraendert: RM-Erweiterung und Gateway-Pfad (06./07.10.), Features von
  10.05/10.06.

### Was ausdruecklich NICHT geprueft wurde

- (Nachtrag 2026-10-11, nach der Veroeffentlichung geprueft: DK5EN-92 und DK5EN-1 haben dieses Release
  per Auto Update im Dev-Kanal installiert -- TLS-Download, Safeboot-Apply, Boot, `tag=v4.40a.10.11`.
  Auf dem T-Beam sinkt der freie Heap beim Download auf 13.952 B, also nur rund 2,4 KB Reserve unter
  der 76-KB-Schwelle.)
- Heltec V4 Fern-LED (gebaut, ungetestet); andere `BOARD_LED`-Boards nur gebaut.
- RM: Sync-Limiter und RM-DUP nicht auf Hardware; RM-GWRELAY nur gegen einen Mock-Server; ein spaetes
  Echo kann nach "No answer" (171 s) noch ausgefuehrt werden; Gateways laden RM1-Befehle zum
  oeffentlichen Server hoch; T-Echo und T114 als RM-Ziel ungetestet.
- ETH-04 im Feld (DB0AIS); `always-broadcast=yes` offen. Auf der Bank nicht erneut gelaufen.
- MTU im AP-Pfad, im klassischen Safeboot und auf T-ETH (akzeptiert).
- Alle uebrigen Boards sind gebaut, nicht gebencht; `t5_epaper` ist nicht im Release.

---

## Release v4.40a.10.06 (Montag, 2026-10-05, datiert 2026-10-06)

`v4.40a.10.05` plus die Web-GUI fuer drei der neuen Features. `FLASH_VERSION` 20261006,
`FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben erhalten. Safeboot
unveraendert gegenueber 10.05, das Release geht also per OTA und per Auto Update (Dev-Kanal) auf
Knoten, die schon das Safeboot von 10.05 haben.

Inhalt: Firmware-Update-Karte mit einer Auswahl off/prod/dev (prod/dev installieren automatisch,
`notify` bleibt Konsole), MTU-Karte 1280/1400/1500/eigener Wert mit MSS-Anzeige, Karte
Fernsteuerung mit Status dieses Knotens und Sendefeld fuer einen anderen Knoten (`POST /rmsend`,
`GET /rmstatus`, Passwort nie geloggt oder gespeichert, Antworten per HMAC geprueft, auch ueber den
Server). Konsolenbefehl `--rm` heisst jetzt `--remotemgmt`; `node_rm` und der Web-Schluessel `rm`
bleiben. Kopfzeile "Meshcom 4.40a". Absolute Fristen in beiden Web-Body-Lesern gegen langsam
tropfende Clients.

Warum 10.06 statt 10.05.2: der Versionsvergleich von Auto Update kennt nur `v<VER>.MM.DD`. Ein
`.2`-Tag waere fuer Auto Update unsichtbar, auch fuer alle Knoten mit 10.05. Same-day-Releases
werden deshalb auf den Folgetag datiert, solange der Parser kein Suffix kennt.

### Was fuer dieses Release auf Hardware geprueft wurde

- Host: 1983 Testfaelle in 60 Umgebungen gruen, Golden-Selftest, pytest- und Tool-Suiten.
- Bauen: 32 Release-Umgebungen mit `MC_BUILD_TAG=v4.40a.10.06`.
- DK5EN-1 (Heltec V3): Web-Karten Auto Update (Auswahl und Rueckmeldung), MTU, Kopfzeile;
  Sendefeld Fernsteuerung an DK5EN-90 ueber LoRa: status, sync, display off, display on mit
  gepruefter Antwort (8 gesendet, 7 beantwortet, ein verlorener Rahmen, Wiederholung beantwortet);
  Web-Schalter `rm` laeuft ueber `--remotemgmt`. Nach der Veroeffentlichung: Auto Update Dev-Kanal
  fand 10.06, Download von GitHub 1,11 MB in 10,8 s, Safeboot entpackte 1,74 MB in 10,6 s, Knoten
  bootet `tag=v4.40a.10.06`.
- DK5EN-90 (RAK4631): `--remotemgmt on|off` und Hilfezeile an der Konsole; Befehle ausgefuehrt,
  Zaehler und Befehlslog in `/rmstatus`.

### Was ausdruecklich NICHT geprueft wurde

- Abweisung eines falschen Passworts auf der Bank (der Zaehler ging beim Neuflashen verloren;
  durch Unit-Tests abgedeckt).
- Alle Boards ausser Heltec V3 und RAK4631 fuer die neuen Web-Karten.

---

## Release v4.40a.10.05 (Montag, 2026-10-05)

`fork-dev` auf dem offiziellen `v4.40a` plus die fuenf Features fuer die offenen upstream-Issues
#1187-#1191 und die Backlog-Wellen seit `v4.40a.10.02`. Anders als 10.02 ist dieser Build NICHT
byte-gleich mit upstream. `FLASH_VERSION` 20261005, `FLASH_STRUCT_VERSION` 20260724 unveraendert --
die Einstellungen bleiben erhalten. Neues Safeboot-Image (Staged-Apply, MTU); es kommt nur per
Web-Flasher oder USB auf den Knoten, OTA schreibt nur `ota_0`.

Inhalt: Auto Update (#1187, ESP32, aus; komprimierte `.bin.zz`-Assets, fuenf GitHub-Wurzeln,
Safeboot entpackt mit dem ROM-tinfl, klassischer ESP32 pausiert BLE fuer TLS, altes Safeboot wird
nach einer Uebergabe erkannt), S&F fuer PNs vom Server und `--stor` (#1188, STOR aus bis zur
Freigabe durch den Server-Betreiber), RM1-Fernsteuerung per LoRa mit HMAC (#1189, aus), MTU auf
allen Boards mit Default 1280 (#1190), BLE-Reconnect-Haertung (#1191). Dazu TZ-Regeln, RTC in UTC,
Web-Haken, EXTUDP-Abwehr von Broadcast/Multicast, T-Deck-Kartenmarker und Kachel-Cache, softAP auf
frischen Knoten. Konzept: `docs/concept-open-issues-20261004.md`.

Dieses Release ist zugleich der Test des Dev-Kanals von Auto Update gegen das eigene Repository.

### Was fuer dieses Release auf Hardware geprueft wurde

- Host: 1977 Testfaelle in 60 Umgebungen gruen, Golden-Selftest 48 Kommandos, 764 pytest-Faelle.
- Bauen: 32 Release-Umgebungen mit `MC_BUILD_TAG=v4.40a.10.05`.
- DK5EN-1 (Heltec V3): MTU in App und Safeboot, BLE 50+20 Zyklen, RM in beide Richtungen, Auto Update
  Ende-zu-Ende ueber das Bank-LAN (Staging, Safeboot entpackt 1,72 MB in 10,3 s, neue Version bootet),
  GitHub-Check auf beiden Kanaelen, Web-GUI.
- DK5EN-90 (RAK4631): BLE 50+20 Zyklen, Store-Knoten und STOR gegen den Mock-Server, RM inkl.
  Zaehler ueber Neustart und Replay-Abwehr.
- DK5EN-92 (T-Beam v1.2): Auto-Update-Check mit BLE-Pause, danach BLE-Verbindungen 3/3.

### Was ausdruecklich NICHT geprueft wurde

- Der Download eines echten GitHub-Releases bis zur Installation (wird mit diesem Release selbst geprueft).
- Die Erkennung eines alten Safeboot wurde auf DK5EN-92 geprueft (Staging + Uebergabe an das alte 4.40a-Safeboot, App meldet `safeboot;old`).
- STOR gegen den echten Server, Messungen M1-M4.
- MTU im AP-Pfad, im klassischen Safeboot und auf T-ETH.
- Alle Boards ausser den drei oben genannten (gebaut, nicht gebencht).

---

## Release v4.40a.10.02 (Freitag, 2026-10-02)

Offizielles `v4.40a`, gebaut aus dem Zweig `fork-dev`. Der Firmware-Quelltext (`src/`, `variants/`,
Safeboot-Images) ist byte-gleich mit upstream `e1e2acea` (Merge von PR #1186); dieses Release enthaelt
keine eigene Firmware-Aenderung. Es existiert, damit der Web-Flasher Images aus diesem Repository
ausliefert. ESP32-Anwendungsbilder sind nicht byte-reproduzierbar, der Code ist derselbe.
`FLASH_VERSION` 20260929 und `FLASH_STRUCT_VERSION` 20260724 unveraendert -- die Einstellungen bleiben
erhalten. Inhalt von 4.40a: Store-and-Forward (aus), Nachbarschaftsmatrix statt MHeard/Pfadtabelle,
19 neue Kommandos, Fehlerkorrekturen (siehe `docs/archive/pr-neo-beschreibung-20261001.md`).

**Zweigmodell ab heute:** `fork-dev` = `upstream/dev` plus ein Portierungs-Commit (Tests, Werkzeuge, Doku,
Pages) plus ein Folge-Commit, der sechs abgedriftete Firmware-Dateien und die Safeboot-Images auf upstream
zurueckstellt. `fork-neo-test` ist als Tag `archive/fork-neo-test-20261002` eingefroren, `fork-neo`,
`master` und die PR-Zweige sind geloescht. Der Web-Flasher bietet nur noch dieses Release an.

### Was fuer dieses Release auf Hardware geprueft wurde

- Host: 1618 Testfaelle in 48 Umgebungen gruen, Golden-Selftest 115 Tests gruen.
- Bauen: 32 Release-Umgebungen.
- Bank vom 2026-10-01 auf demselben Quelltext: RAK4631 (DK5EN-90) und Heltec V3 (DK5EN-98) mit 4.40a
  geflasht, Rufzeichen und Einstellungen blieben erhalten, `--postime 3600` ueberlebt den Neustart, die
  Matrix-Kommandos liefern im Live-Mesh plausible Werte.

### Was ausdruecklich NICHT geprueft wurde

- Der Mehrtages-Soak: der Nachtlauf 2026-10-01 23:28 bis 2026-10-02 08:00 auf fuenf Knoten ist beim
  Schnitt des Releases nicht ausgewertet.
- T-Beam und T-Deck vor dem PR (nur im Nachtlauf dabei); alle uebrigen Boards sind gebaut, nicht gebencht.
- Das Flashen dieses Release auf Bank-Knoten.

---

## Release v4.35v.09.29-neo (Dienstag, 2026-09-29)

Offizielles `v4.35v` plus die neo-Linie plus eine Kampagne zu BLE, Batterie und Zeitzaehlern.
`FLASH_VERSION` 20260929, `FLASH_STRUCT_VERSION` unveraendert -- die Einstellungen bleiben erhalten.
Ersetzt alle frueheren Releases, auch im Web-Flasher. Plan, Befunde und Bench-Ergebnisse:
[`docs/ble-batt-campaign-20260929.md`](docs/ble-batt-campaign-20260929.md).

**BLE (Knoten -> Telefon):** Beide Drains nahmen einen Rahmen aus dem Ring, bevor er gesendet war,
und ignorierten das Ergebnis -- NimBLEs `notify()` ohne Argumente kann keinen Fehler melden, Bluefruits
`write()` liefert 0 bei voller SoftDevice-Queue. Jetzt bleibt ein abgelehnter Rahmen liegen und wird
im naechsten Fenster wiederholt, nach fuenf Versuchen verworfen und gezaehlt (`src/ble_phone_drain.h`,
BLE-N1). Die ausgehandelte MTU wird gelesen, zu lange Rahmen werden gezaehlt (BLE-N2), `--info` zeigt
`tx s r d t e mtu`. Ein Wettlauf zwischen Lesen und Verdraengen auf nRF52 ist zu (BLE-N3,
`bf_peek_gen()`/`bf_pop_if()`), und der RAK trennt ein Telefon mit falscher PIN sofort statt erst beim
naechsten Schreiben (BLE-N4, auf der Bank gefunden).

**Batterie:** Konzept `docs/archive/concept-battery-consolidation-20260923.md` vollstaendig -- ein
gemeinsamer Filter (Zeitkonstante 30 s), ein Prozentverlauf fuer alle Boards (1S und 2S), ein
BAT-01-Detektor statt zwei, 100-ms-Takt statt der neo-Regression von 30 s. Beim Heltec V3/V4/Stick
ist der Teiler nur noch 100 ms je 30 s eingeschaltet -- die rote LED bleibt ohne Akku nicht mehr
30 s an. USB-Boards ohne Zelle melden 0 % und senden kein `/B=` mehr (vorher 100 %).

**Zeit:** Die Minutenstempel der Nachbarschaftsmatrix sprangen beim `millis()`-Ueberlauf (49,7 Tage)
von 6046 auf 0, der naechste Sweep loeschte die ganze Matrix (TIME-01, `uptimeMin16()` an 39
Stellen). Gefunden mit den neuen Teleport-Tests (`test/support/teleport_clock.h`). Der Batteriefilter
fror nach einem Zeitsprung >= 2^31 ms ein (TIME-02).

**Weiter:** INS-05 (bewusster Neustart hinterlaesst keinen Loop-Abschnitt), DM-17 (`:rej` zaehlt
nicht als ACK), GW-02; Soak-Befunde F1-F8 vom 28.09. (`15fa385a`); Zeichenzaehler der Weboberflaeche
zaehlt Bytes (#1173); RAK4631 mit `-Os` statt `-Ofast` (Flash 95,5 % -> 69,2 %). Aus offiziell
`v4.35v`: DL-Internet-Server, HAMNET-NTP-Rueckfall, WIDE1-1 im Track-Beacon (#1174), Einmal-Position
ohne TRACK-Punkt sendet die Knotenposition (#1175, makrohard, hier per Cherry-Pick).

Tests: 1588 Host-Faelle in 48 nativen Envs, alle gruen, dazu der Golden-Selftest. 32
Release-Envs gebaut.

### Was fuer dieses Release auf Hardware geprueft wurde

- Heltec V3 (DK5EN-1): BLE-Burst 5/5 vollstaendig, 6/6 kaputte Rahmen ueberlebt, PIN mit und ohne
  Code, 40/40 Gruppe-9-Nachrichten vom Mock-Server beim BLE-Client, Batterie ohne Zelle nach dem
  Erkennungsfenster 0,00 V / 0 %.
- RAK4631 (DK5EN-90, `-Os`): BLE-Burst 5/5, 6/6 kaputte Rahmen, PIN 3/3 (nach BLE-N4),
  Settings-Rueckschreiben ueber 0x95 ohne Aenderung an 92 Feldern, LoRa senden und empfangen gegen
  den Heltec, Ethernet und NTP.
- DK5EN-98 (Heltec V3): Soak auf diesem Stand seit 2026-09-29 10:07, laeuft bis 2026-10-02 10:10.
- Die Bench-Images unterscheiden sich von diesem Tag nur durch den #1175-Cherry-Pick und den
  Flash-Stempel.

### Was ausdruecklich NICHT geprueft wurde

- Alle anderen Boards: gebaut, nicht auf der Bank -- die Batterieaenderung betrifft sie alle.
- Die Heltec-LED mit eigenen Augen; das 100-ms-Fenster ist in einer Host-Simulation gemessen.
- Der `node_msgid`-Fix (`dbc57632`) ueber die Settings-Characteristic des RAK.
- Die Auswertung des laufenden Soaks.
- Offen und bekannt: Kommandoflut schneller als etwa 3/s ueberlaeuft den Config-Ring (gezaehlt),
  UDP-Stack verliert bei 50 Datagrammen/s, Heltec erkennt "kein Akku" erst nach 3 Minuten, DM-18
  (`:rej` liest die falsche Nachrichten-ID), T114/T-Echo weiter mit `-Ofast`.

---

## Release v4.35u.09.28.3-neo (Montag, 2026-09-28)

Dritter Schnitt desselben Tages, auf `v4.35u.09.28.2-neo`. Versionsbuchstabe und `FLASH_VERSION`
bleiben unveraendert (20260928) -- Unterschied sind der Git-Commit (Fix `ea16bfef`) und die
Build-Zeit in `--info`. Ersetzt alle frueheren Releases, auch im Web-Flasher.

**Fehlerbehebung (Issue #1173):** Der Browser schickt jeden Formularwert prozentkodiert (`ą` als
`%C4%85`). `decodeURLPercentCoding()` in `src/web_functions/web_functions.cpp` dekodierte aber nicht,
sondern ersetzte eine feste Liste von rund 60 Codes (deutsche Umlaute, `ß`, einige italienische
Vokale, ASCII-Satzzeichen) und liess jeden anderen Code woertlich stehen. Polnisch, Tschechisch,
Kyrillisch, Emoji usw. gingen deshalb als Text wie `%C4%85` auf HF. Ausserdem wurde `%25` vor den
spaeteren Codes dekodiert, ein getipptes `%28` kam als `(` an.

Neu ist `src/url_decode.{h,cpp}`: ein Dekoder in einem Durchlauf, jedes `%XX` wird sein Byte. Die
alten Eigenheiten bleiben (`+` wird Leerzeichen, Zeilenumbruch wird `-`, `"` faellt weg), dekodierte
Steuerzeichen fallen jetzt ebenfalls weg, damit `%00` nichts abschneidet. `decodeURLPercentCoding()`
ruft nur noch diesen Dekoder auf -- Setup-Seite, Funktionsaufrufe und Login profitieren mit.
Nebenbei: die 150-Byte-Kuerzung in `send_message()` schneidet an einer Zeichengrenze
(`charset_utf8_safe_truncate()`), das JavaScript `updateMessages()` ruft kein `decodeURIComponent()`
mehr auf die rohe HTML-Antwort auf (warf bei jedem `%` einen Fehler und liess das Panel stehen, und
verdeckte den Fehler auf dem Bildschirm des Senders), und HTML-Antworten tragen
`charset=utf-8` im HTTP-Header.

Upstream angeboten als [PR #1178](https://github.com/icssw-org/MeshCom-Firmware/pull/1178) (nur
Quellcode). PR #1176 und #1177 sind inzwischen upstream gemergt.

Tests: 889 Host-Faelle in den 12 Gate-Envs, alle gruen, davon 14 neue
(`test_url_decode`); gegen eine C-Portierung des alten Dekoders schlagen 6 davon fehl. 32
Release-Envs gebaut.

### Was fuer dieses Release auf Hardware geprueft wurde

Der Fix selbst, ueber die Weboberflaeche eines Heltec V3 (DK5EN-1): mit dem Vorstand kam `ąęś äö`
an Gruppe 9 als `%C4%85%C4%99%C5%9B äö` im Nachrichtenspeicher an; nach OTA auf diesen Stand steht
`ąęśćłóżź äö 100% ok` exakt so im Speicher, und der HTTP-Header traegt `charset=utf-8`.

### Was ausdruecklich NICHT geprueft wurde

- Empfang auf einem zweiten Knoten -- geprueft ist der Nachrichtenspeicher des sendenden Knotens,
  der genau den Text haelt, der ans Funkmodul geht. Gruppe 9 erscheint nicht im Server-Archiv.
- Der Fix auf einem anderen Board als dem Heltec V3; die nRF52-Builds (RAK4631, T114, T-Echo)
  kompilieren den Dekoder, wurden aber nicht benutzt.
- Die Zeichenanzeige unter dem Eingabefeld zaehlt weiter Zeichen statt Bytes; bei vielen
  Mehrbyte-Zeichen kuerzt der Knoten vor dem angezeigten Limit (sauber an einer Zeichengrenze).
- Alles, was `v4.35u.09.28.2-neo` als offen auffuehrt, gilt unveraendert weiter.

---

## Release v4.35u.09.28.2-neo (Montag, 2026-09-28)

Zweiter Schnitt desselben Tages auf `v4.35u.09.28-neo` (`fed0eb0e`). Versionsbuchstabe und
`FLASH_VERSION` bleiben unveraendert (20260928) -- einziger Unterschied ist der Git-Commit
(`8de8d3da`).

**Fehlerbehebung:** `is_format_char()` in `src/charset_filter.cpp` behandelte U+200D (ZERO WIDTH
JOINER) als Teil des pauschalen Bereichs U+200B-U+200F und entfernte es aus jeder Nachricht. Anders
als seine Nachbarn im selben Block traegt U+200D kein eigenes Glyph, sondern verbindet die
Codepoints links und rechts davon zu einem einzigen Schriftzeichen -- z. B. Shrug-Symbol (U+1F937)

- ZWJ + Male-Sign (U+2642) + Variation-Selector (U+FE0F) ergibt "person shrugging" nur, wenn das
  ZWJ erhalten bleibt. Das bisherige Entfernen zerlegte die Sequenz in zwei getrennte Glyphen statt
  ein unsichtbares Zeichen zu entfernen.

Gefunden per Bankversuch auf einem RAK4631 (`--loradebug on` / `--txcapture on`): der `TX_FRAME`-
Mitschnitt der tatsaechlich gesendeten Bytes zeigte die fehlende 3-Byte-ZWJ-Sequenz (E2 80 8D)
zwischen den beiden Emoji-Haelften. Die Ausnahme fuer U+200D existiert bereits im separaten
Web-Proxy-Projekt (MCProxy, `text_decode.py`, dort seit 2026-08-30 fuer denselben Fehlerfall);
dieser Fix portiert dieselbe, bereits bewaehrte Ausnahme in den Firmware-eigenen Zeichensatzfilter.
Alle anderen Zeichen im Bereich U+200B-U+200F (Zero Width Space, Zero Width Non-Joiner, Bidi-
Override/Embedding, BOM) werden weiterhin entfernt.

Auch upstream angeboten als [PR #1177](https://github.com/icssw-org/MeshCom-Firmware/pull/1177)
(nur Quellcode, ohne Testaenderungen, da upstream die native Testsuite dieses Forks nicht
mitfuehrt).

Tests: 875 Host-Faelle in den 12 Gate-Envs, alle gruen, davon 2 neue Regressionstests
(`test_charset_filter`). 32 Release-Envs gebaut.

### Was fuer dieses Release auf Hardware geprueft wurde

Der Fix selbst, Ende zu Ende, auf einem RAK4631 (DK5EN-90): Vor-Fix-Stand geflasht, Shrug-Emoji per
Seriell-Konsole gesendet, `TX_FRAME`-Mitschnitt zeigt fehlende ZWJ-Bytes; Fix geflasht, derselbe
Versand wiederholt, `TX_FRAME`-Mitschnitt zeigt die ZWJ-Bytes intakt und die Konsolenzeile ein
zusammengesetztes Glyph statt zweier getrennter Zeichen.

### Was ausdruecklich NICHT geprueft wurde

- Der Fix auf einem anderen Board als dem RAK4631 -- der Zeichensatzfilter ist board-unabhaengiger
  gemeinsamer Code, aber kein anderes Board hat in dieser Session ein ZWJ-Emoji gesendet.
- Ein Relay auf aelterer Firmware entfernt das ZWJ weiterhin, wenn es die Nachricht neu kodiert und
  weiterleitet -- im selben Bankversuch beobachtet (ein Nachbarknoten noch auf `v4.35u.09.28-neo`
  hat das ZWJ beim Relayen erneut entfernt). Erwartet, bis dieser Knoten neu geflasht ist, kein
  Fehler dieses Fixes.
- Alles, was `v4.35u.09.28-neo` als offen auffuehrt, gilt unveraendert weiter.

---

## Release v4.35u.09.28-neo (Montag, 2026-09-28)

Upstream hat am 27.09. abends das offizielle `v4.35u` veroeffentlicht (`61a58daa`, upstream/dev
nach #1162 und #1165 bis #1172). Dieser Stand zieht den Versionsbuchstaben nach und enthaelt den
Code aller dieser PRs, ohne upstream/dev zu mergen: die Basis bleibt `6cc8b552`, die noch fehlenden
PRs sind gezielt portiert. Gebaut von `fork-neo-test`, dem Hauptzweig seit dem Aufraeumen der Zweige
am 27.09. (`fork-main`, `feature-snf`, `feature-neighbour-matrix` stillgelegt, Tags
`archive/<name>-20260927`).

`SOURCE_VERSION_SUB` und `SOURCE_VERSION_WEB_SUB` gehen auf `u`. `FLASH_VERSION` bleibt 20260928
wie bei `v4.35t.09.28-neo` -- der Buchstabe im Versionsfeld ist der einzige Unterschied im
`--info`. `FLASH_STRUCT_VERSION` bleibt 20260724, die Einstellungen bleiben erhalten.

**Portiert aus upstream (nicht in `v4.35t.09.28-neo`):** #1171 BLE-Supervision-Timeout 4 s statt
1,8 s plus `[BLE ]`-Diagnose; #1169 RX-Log aelteste Zeile zuerst, angepasst an den erst bei Bedarf
angelegten RX-Log-Puffer; #1165 `--setcall` mit dem schon gesetzten Rufzeichen schreibt nicht und
startet nicht neu; #1166 Netzkonsole im Ethernet-Modus, dazu `DISABLE_BLE` und `DISABLE_BATTERY`
(der Batterie-Schalter sitzt in `loopAction_battCheck()`, weil neo die Batteriemessung in den
Scheduler verlegt hat). #1164 ist nicht portiert und bleibt upstream offen.

**Eigene Korrekturen:** der BLE-Kommandoring fasst jetzt den ganzen Konfigurations-Burst (3072 Byte
auf allen Boards, `static_assert` dagegen), der Verlaufs-Iterator liest unter der Ringsperre -- beide
aus #1157, im neo-Byte-Ring nie nachgezogen. `handleACK()` faltet die msg_id einer
Wiederholungskopie auf das Original zurueck, bevor es nachschlaegt (App-Haken, Stopp der
Wiederholung), und die Server-Echo-Pruefung beider UDP-Pfade ebenso. Upstream hat den
`handleACK()`-Fehler auch: PR #1176.

Tests: 1408 Host-Faelle in 46 Envs, 1407 gruen, 1 uebersprungen, 32 Release-Envs gebaut,
Advisor-Pass ohne Nacharbeit. `fork-neo` bzw. `icssw-org/dk5en-neo` sind mit demselben Stand neu
abgeleitet (Basis `6cc8b552`, gate.sh 38/40 wie erwartet, keine Symbolabweichung).

### Was fuer dieses Release auf Hardware geprueft wurde

Nichts. Kein Board hatte Bankzeit auf diesem Stand.

### Was ausdruecklich NICHT geprueft wurde

- Die vier portierten upstream-PRs auf Hardware (BLE-Timeout mit iOS/Android, RX-Log-Reihenfolge,
  `--setcall`, Netzkonsole auf T-ETH-ELITE).
- Der App-Haken fuer eine per Relais quittierte PN-Wiederholung, Ende zu Ende.
- Ein Board mit `DISABLE_BLE`/`DISABLE_BATTERY` -- einmal mit beiden Schaltern gebaut, keines
  liefert sie aus.
- Der 24-h-Soak von DK5EN-1 und DK5EN-98 bis 28.09. 17:39 laeuft auf dem Stand `ce9bf157`
  (`v4.35t.09.28-neo`), nicht auf diesem.

---

## Release v4.35t.09.28-neo (Sonntag, 2026-09-27, datiert auf 2026-09-28)

Gebaut am Samstagabend/Sonntag 27. September, auf Betreiberwunsch auf den 28. September datiert --
derselbe Vorgriff wie schon bei `v4.35t.09.27-neo`. Inhaltlich: `v4.35t.09.27-neo` unveraendert
(Nachbarschaftsmatrix, Speichern und Weiterleiten fuer Direktnachrichten) plus ein neuer
Wiederholungsmechanismus fuer persoenliche Nachrichten (PN, Direktnachrichten) mit XOR-msg_id, dazu
die Entfernung von `--dmretry` und dem Ausgangskorb. Gebaut von `feature-snf`.

`FLASH_VERSION` geht auf 20260928 (Boot-Log und `DM`-Setlog-Zeile), `FLASH_STRUCT_VERSION` bleibt
20260724 -- die Einstellungen bleiben erhalten. Der neue Wiederholungsmechanismus braucht keinen
eigenen Speicher.

**PN-Wiederholung im XOR-Format.** Konzept: `docs/pn-zustellung-dedup.md`; Umsetzung:
`docs/pn-retry-xor-impl-plan.md`; Server-Aenderung (noetig, bevor dieser Stand breit ausgerollt
wird): `docs/pn-retry-server.md`. Wiederholung k (1 bis 3) einer selbst gesendeten PN traegt die
Original-msg_id mit XOR-verknuepften Bits 10-11 und neu berechneter FCS; die Erstsendung bleibt
bytegleich zu vorher. Alte Relais leiten die Kopie damit weiter, statt sie als Duplikat der
Original-msg_id zu verwerfen -- genau das Problem, das dieses Format loest. Ein Echo der eigenen PN
beendet die Wiederholung nicht mehr, nur noch das `:ackNNN` des Ziels, ueber LoRa oder ueber den
Server; ein reiner Server-ACK stoppt jetzt auch den wartenden Ring-Platz. Eine PN im Auftrag eines
KISS-Clients bleibt beim alten Verhalten (bytegleiche Wiederholung, Abbruch beim ersten Echo). Ein
Wettlaufbefund zwischen einem eintreffenden ACK und einer noch in der Warteschlange stehenden
Wiederholungskopie ist behoben (M1): vorher konnte die Kopie trotz ACK noch hinausgehen und ihr Echo
die Wartezeit neu starten -- bis zu drei ueberzaehlige Kopien und am Ende ein faelschliches
"failed" nach dem ACK.

**`--dmretry` und der Ausgangskorb sind entfernt.** Mit dem XOR-Format waren Ausgangskorb-Leiter
und Ring-Wiederholung fast identisch geworden, deshalb bleibt nur ein Pfad: jede PN wird einmal
gesendet und bis zu dreimal im Abstand von 40 s wiederholt. Das Kommando `--dmretry off|3|9`, sein
Web-Setup-Feld, die `--info`-Zeile und die Ablehnung `OUTBOX FULL NOT SENT` entfallen (Modus 9 liess
sich mit drei Bitvarianten ohnehin nicht mehr abbilden). Details: `docs/CHANGELOG-snf.md`.

**Vertraeglichkeit, offen ausgesprochen:** das gilt jetzt fuer jede PN, nicht mehr nur fuer
`--dmretry 3`. Aeltere Empfaenger koennen dieselbe PN bis zu viermal zeigen (einmal je
Bitvariante), weil sie ueber die volle msg_id entdoppeln. Aeltere Speicherknoten verzoegern die
Zustellung um bis zu rund 2 Minuten, weil sie jede Kopie als neue Sendung werten und die Haltezeit
jedes Mal neu ansetzen. Ein gespeicherter `dm_retry`-Wert (ESP32-NVS, nRF52 `/dm.cfg`) wird gelesen
und ignoriert; ein Kommando dafuer gibt es nicht mehr. Die Ablehnung `OUTBOX FULL NOT SENT` gibt es
auf dieser Firmware nicht mehr, ihr Textpraefix bleibt aber in der Echo-Wache stehen, damit das
Wiederholungsformat aelterer Absender weiter erkannt wird. **Der zentrale MeshCom-Server muss vor
einer breiten Verteilung auf `msg_id & 0xFFFFF3FF` entdoppeln** (`docs/pn-retry-server.md`); bis
dahin kann eine Region mit mehreren Knoten auf diesem Stand die Wiederholungen einer PN ueber den
Server als getrennte Nachrichten sehen.

**Auch in diesem Schnitt:** die Soak-Werkzeuge `tools/bench/soak_dm.py` (Test-DMs zwischen zwei
eigenen Knoten ueber deren Webserver, nie `*` oder eine Gruppe) und `tools/soakstatus.py`
(Auswertung eines Mitschnittfensters: Neustarts, Heap-Drift, DM-Zaehler, Wiederholungsmarker,
Quittungszeit je Test-DM, NBR-Konsistenz). Protokoll: `docs/archive/soak-xor-20260927.md`. Dazu eine
Luecke aus der Ausgangskorb-Entfernung geschlossen: `native_udp_frame_twin` verlor seine
Attrappe fuer den neuen Server-ACK-Ring-Stopp und liess sich zeitweise nicht mehr linken; jetzt
mit passender Attrappe und einem Test fuer den eigenen-DM-Server-ACK-Zweig.

Nicht uebernommen aus upstream, unveraendert seit `v4.35t.09.27-neo`: `5efa2171` und die zwei
Folgecommits (auskommentierte Wiederholbits im `msg_id`), kein Verhaltensunterschied. Die
PN-XOR-Wiederholung selbst ist inzwischen upstream: [PR #1168](https://github.com/icssw-org/MeshCom-Firmware/pull/1168),
gemergt in `dev` am 27. September.

**Gates:** 45 native Host-Umgebungen, 1384 Testfaelle;
`test/golden/selftest.sh` gruen; 32 Release-Umgebungen gebaut, RAK4631-Flash 96,1 %
(783 580 von 815 104 B, vorher 96,4 %).

### Was fuer dieses Release auf Hardware geprueft wurde

- **PN-XOR-Bank, 27. September, DK5EN-1 und DK5EN-98 (beide Heltec V3), direkte LoRa-Strecke:**
  beide Knoten laufen seit 17:25/17:32 auf genau diesem Firmware-Stand (Build `ce9bf157`; die
  beiden Commits danach fuegen nur Soak-Werkzeuge und den `FLASH_VERSION`-Stempel hinzu, keine
  Quelltextaenderung). Ein 24-Stunden-Dauerlauf laeuft seit 17:39; der Zwischenstand nach rund
  20 Minuten war PASS auf jedem Kriterium: kein Neustart, Heap stabil, jede Test-DM quittiert
  (eine in 4,6 s ueber den Server-Pfad, eine in 10,0 s ueber direktes LoRa, keine brauchte eine
  Wiederholung), 0 NBR-Konsistenzverletzungen. Beide Knoten hoeren sich direkt und stark, dieser
  Lauf belegt 24-Stunden-Stabilitaet und den Quittungspfad, nicht die Wiederholung bei
  Mehrfach-Hop-Verlust. Der endgueltige 24-Stunden-Befund steht beim Schreiben dieses Textes noch
  aus.
- Alle anderen Boards der 32 Release-Umgebungen (inklusive RAK4631): gebaut, keine Bank-Zeit fuer
  das XOR-Format in diesem Zyklus.

### Was ausdruecklich NICHT geprueft wurde

- Der endgueltige 24-Stunden-Befund des laufenden Dauerlaufs.
- Wiederholung bei Mehrfach-Hop-Verlust (`BACKLOG` PN-01) -- DK5EN-1 und DK5EN-98 hoeren sich
  direkt, es fehlt ein Knotenpaar mit echtem Mehrfach-Hop- oder Grenzsignalpfad.
- Die maskierte Entdopplung auf dem zentralen Server -- noch nicht ausgeliefert.
- Alles, was fuer `v4.35t.09.27-neo` unten schon als ungeprueft steht.

## Release v4.35t.09.27-neo (Sonntag, 2026-09-27)

Der Sonntagsstand vom 27. September: `v4.35t.09.26-neo` plus zwei neue
Funktionen, gleichgewichtig -- die Nachbarschaftsmatrix (MeshCom-5-Topologie,
Stufen 1 bis 3, Branch `feature-neighbour-matrix`) und Speichern und
Weiterleiten fuer Direktnachrichten (DM-Stufen 0, 1, 2.1, 3 und 4 aus
`fork-main`, portiert auf Branch `feature-snf`). Gebaut von `feature-snf`.
Damit ist dieses Release nicht mehr "upstream/dev plus besserer Code": es
bringt neues Verhalten, auch auf dem Funk. Wer den reinen neo-Stand will,
nimmt `v4.35t.09.26-neo`; beide stehen im Web-Flasher.

`FLASH_VERSION` geht auf 20260927 (Boot-Log und `DM`-Setlog-Zeile),
`FLASH_STRUCT_VERSION` bleibt 20260724 -- die Einstellungen bleiben erhalten.
Die neuen S&F-Einstellungen liegen in eigenem Speicher (ESP32 NVS-Keys, nRF52
`/dm.cfg` und `/msgstore.cfg`). Das Build-Datum in `--info` ist der Abend des 26. September. Ausnahme beim Update: ein Knoten, der eine
Nachbarschaftsmatrix-Testversion vor dem 25. September hatte (nur
DK5EN-Testknoten), kommt mit KISS/TCP und KISS-TX an hoch und braucht sofort
`--kiss tx off`, `--kiss meta off`, `--kiss off`.

Nachbarschaftsmatrix: ein Topologiespeicher (Kantenpool, Direktnachbarn,
Horizont) ersetzt MHeard-Tabelle, Pfadtabelle und die dichte Matrix; 64/256
Nachbarn/Kanten auf klassischem ESP32, 128/512 auf S3 und nRF52, bei weniger
statischem RAM als vorher. NCNT ist symmetrisch und auf dem Funk auf 99
gekappt. Relay-Entscheidung je Frame mit `--nbrrelay off|count|on` (Standard
off), HN-Nachbarschaftsmeldung mit `--nbrreport off|auto|on` (Standard auto:
alle 15 min von Knoten ohne Mesh und ohne Gateway, `max_hop 0`). Details in
[`docs/CHANGELOG-meshcom5.md`](docs/CHANGELOG-meshcom5.md).

Speichern und Weiterleiten: doppelte DMs werden erneut quittiert (hoechstens
einmal je 30 s), aber nicht doppelt angezeigt; eine gescheiterte DM meldet
Status `0x03`; `{` im DM-Text geht als `(` hinaus; optionaler Ausgangskorb mit
Wiederholleiter (`--dmretry off|3|9`, Standard off); Speicherknoten auf
ESP32-S3 und RAK4631 (`--store off|own|list|heard`, Standard off) mit
Mailbox-Seite und `:sto`-Gewahrsamsmeldung (Status `0x04` in der App). Details
in [`docs/CHANGELOG-snf.md`](docs/CHANGELOG-snf.md).

Nicht uebernommen aus upstream: `5efa2171` und die zwei Folgecommits
(Wiederholbits im `msg_id`); upstream hat die Maske selbst auskommentiert,
kein Verhaltensunterschied zu `v4.35t.09.26-neo`.

**Gates:** 44 native Host-Umgebungen, 1379/1379 Testfaelle; `test/golden/selftest.sh` gruen (die Baseline
`variant-ini-effective.json` nachgezogen: vier Schluessel, genau die
S&F-Quellen und -Tests der Host-Umgebungen); alle 32 Release-Umgebungen
gebaut, RAK4631-Flash 96,4 % (785 504 von 815 104 B).

### Was fuer dieses Release auf Hardware geprueft wurde

Dieses exakte Image lief auf keinem Board. Die Bank-Laeufe unten liefen mit
Vorstufen von `feature-snf` bzw. `feature-neighbour-matrix`; der Unterschied
zum Release ist der Nachzug aus `fork-main` (Logzeilen, `SN1` nach
`--webpwd`, Ping-Meldung) und der `FLASH_VERSION`-Stempel, nicht der Code der
beiden Funktionen.

- **S&F-Bank 2026-09-26, nur LoRa, 2 dBm, zwei Laeufe, beide PASS:** RAK4631
  DK5EN-90 als Speicherknoten (`--store heard`), T-Beam DK5EN-92 als
  Empfaenger, Heltec V3 DK5EN-1 als Sender (auf `0d4b914c`). DM an den
  abwesenden T-Beam gehalten (Mailbox-Seite zeigt HELD), `:sto` beim Sender,
  nach Rueckkehr Zustellung ueber einen Hop, Quittung beim Sender. Logs in
  `docs/bench/snf-20260926/`.
- **Nachbarschaftsmatrix:** T-Deck Plus (Topologie ueberlebt den Neustart,
  Nachbarn nach 78 s wieder da), Heltec V3, T-Beam v1.2, RAK4631-Stresslauf
  (2 Laeufe, 0 Konsistenzverletzungen, kein Reset, Heap stabil); Replay des
  Feldmitschnitts 21.-24.09. gegen eine eingefrorene Kopie des alten Codes
  (0 abweichende Entscheidungen in 2697 Frame-Gruppen).

### Was ausdruecklich NICHT geprueft wurde

- Das Release-Image selbst, auf keinem Board.
- Der groesste Teil des S&F-Testplans: `--dmretry`-Leiter, Sender auf dieser
  Firmware mit "gehalten" in der App, zwei Speicherknoten, Speicherknoten mit
  Gateway, Neustart mit offenen Eintraegen, Ablauf und Cooldown, Grenzen bei
  25 DMs, Mailbox-Knoepfe Deliver/Purge. Liste mit Test-IDs in
  `docs/snf-port-campaign.md`.
- Die Auswertung des Nachbarschaftsmatrix-Feldlaufs auf DK5EN-1 und
  DK5EN-98 (laeuft seit 26.09.).
- `--nbrrelay on` ausserhalb der zwei DK5EN-Feldknoten.
- Alle anderen Boards der 32 Release-Umgebungen: gebaut, nicht auf der Bank.
- Alles, was fuer `v4.35t.09.26-neo` unten schon als ungeprueft steht.

## neo-Release v4.35t.09.26-neo (2026-09-26)

Dritter Nachzug von `upstream/dev` auf den neo-Zweig: Basis `6cc8b552`
(icssw-org hat #1157 bis #1161 gemergt), als `43760732` in `fork-neo-test`
gemergt. neo bleibt dabei, was es ist -- `upstream/dev` plus besserer Code,
keine neue Protokollfunktion, kein neuer Betriebsmodus. Die Firmware meldet
weiterhin `4.35t`, `FLASH_VERSION 20260912` und `FLASH_STRUCT_VERSION 20260724`
exakt wie vorher; der Name "neo" lebt nur im Git-Tag, im Release und im
Web-Flasher. Die Konfiguration der Knoten bleibt beim Update erhalten.

Inhaltlich bringt der Merge vier Bloecke zurueck bzw. neu herein: #1157 ist
die eigene RAM-Arbeit dieses Branches (Byte-FIFO-Ringe, MHeard-Drosselfix,
Web-Header als String), ueber `fork-main` portiert und jetzt wieder upstream;
#1158 (OE1KFR) verschiebt `WSPWD`/`ASYM` von `SN` nach `SN1`, damit das
Knoten-JSON unter dem BLE-Limit bleibt (`f73cbf9e`, mit diesem Merge neu auf
dem Branch); #1159 bis #1161 (OE1KBC) sind Kommentare fuer
`4.35t`, die zwei MSB-Wiederholungsbits im `msg_id` vorbereiten, die Maske
selbst bleibt auskommentiert -- keine Verhaltensaenderung. Aus der
Konfliktaufloesung des Merges selbst: `sendPing()` meldet eine verweigerte
TX-Ring-Eintragung jetzt laut (`[PING]...not queued`) statt sie
stillschweigend zu verwerfen, der BLE-Kommandoring `RING_BYTES_PHONECOM`
waechst auf 3072 B auf jeder Boardklasse, damit der komplette
Konfigurations-Burst hineinpasst, ein neuer Diagnosemarker
`[MC-DBG] RING_OVERFLOW buf=phone` meldet einen Ueberlauf dieses Rings, und
der Byte-Ring-Iterator fuer die Web-Nachrichtenseite zieht seinen Snapshot
jetzt unter `BF_LOCK`. Details, Einstufung (Restrukturierung/Fehlerbehebung)
und Nachweislage stehen als Punkte 124-129 in
[`docs/CHANGELOG-neo.md`](docs/CHANGELOG-neo.md).

Dieses Release ersetzt `v4.35t.09.21-neo`: dessen Release-Objekt und Tag
wurden geloescht, ein Freitext dazu wurde nie committet.

**Gates:** 36 native Host-Umgebungen, 1070/1070 Testfaelle;
`test/golden/selftest.sh` gruen; alle 32 Release-Umgebungen gebaut.

### Was fuer dieses Release auf Hardware geprueft wurde

**Nichts.** Kein Board hat dieses Firmware-Image -- den heutigen Merge-Stand
`43760732` -- je geladen. Als Kontext die fruehere neo-Bank-Historie, die
nicht dieses Image betrifft:

- **Differenzlauf 2026-09-19** gegen `upstream/dev` auf DK5EN-1 (Heltec V3)
  und DK5EN-92 (T-Beam): auf dem Draht kein Unterschied messbar, im Knoten
  +11 296 B respektive +15 560 B freier Heap (Details in
  [`docs/CHANGELOG-neo.md`](docs/CHANGELOG-neo.md), Abschnitt "Auf der Bank
  gemessen"). Zwoelfstuendiger Dauerlauf im Anschluss auf drei Knoten ohne
  Neustart, Absturz oder Ringueberlauf.
- **W4-Bench auf DK5EN-1, 2026-09-25**: KISS/TCP, `IS1`, `SN1` und der
  `--via`-Fix nach dem Upstream-Merge auf `e4a2393f` (siehe
  `docs/archive/neo-upstream-merge-20260925.md`).

Beide Laeufe pruefen fruehere Zwischenstaende dieses Branches, nicht den
heutigen Merge.

### Was ausdruecklich NICHT geprueft wurde

- Der heutige Merge `43760732` selbst, auf keinem Board.
- Die um +1 kB je Boardklasse gewachsene `RING_BYTES_PHONECOM` -- ob der
  komplette Konfigurations-Burst jetzt tatsaechlich ohne Abschneiden bei der
  App ankommt, ist eine Annahme aus der Ringgroesse, keine Messung.
- Der neue `[MC-DBG] RING_OVERFLOW buf=phone`-Marker -- kein Ueberlauf auf der
  Bank provoziert.
- Die verschobenen `WSPWD`/`ASYM`-Felder in `SN1` -- keine App-Sichtpruefung,
  nur Host-Suite.
- Die aufgeloeste `sendPing()`-Fehlermeldung -- kein Bank-Log mit einem
  tatsaechlich verweigerten Ping.
- Der Safeboot-Stand (K15, `a04d9c87`, inhaltsgleich mit dem offenen
  upstream-PR #1162) auf diesem Image -- die Abbruch-Bank 6/6 lief am 25.09.
  auf DK5EN-1 mit fork-main, nicht mit diesem Stand.
- Alles, was in [`docs/CHANGELOG-neo.md`](docs/CHANGELOG-neo.md) unter den
  Kapiteln K01-K18 ohnehin schon als "kein Hardware-Nachweis" oder
  "kompilier-verifiziert" gefuehrt wird (E22_XML- und T-Beam-Headroom u. a.).

## Stability-Release v4.35t.09.12.2 (2026-09-12)

Zehn Punkte des Forks gegenueber `v4.35t.09.10`, Changelog-Punkte 212-221:
die APRS-Parser-Welle (212-216), ein WebGUI-Fix (217), drei Feldfixes gegen
4.35t (218-220) und der Upstream-Sync (221). `FLASH_VERSION` geht auf 20260912,
`FLASH_STRUCT_VERSION` bleibt unveraendert 20260724 -- die Einstellungen der
Knoten bleiben erhalten. Gates: 673 native Testfaelle in 12 Host-Umgebungen,
alle 32 Release-Umgebungen gebaut. Bench in diesem Zyklus: DK5EN-93 (Heltec
V3), DK5EN-98 (Heltec V3, Gateway), DK5EN-14 (T-Deck Plus); dazu die
Feldbestaetigung von OE3LCR fuer Punkt 200.

Der Tag traegt `.2`, weil upstream am Abend des 12. September seinen eigenen
`dev`-Stand als `v4.35t.09.12` getaggt hat (Release ohne Assets zum Zeitpunkt
des Schreibens); ein gleichnamiger Fork-Tag wuerde beim naechsten
`git fetch upstream` kollidieren. Dieses Release ersetzt `v4.35t.09.10`, das
Release-Objekt wurde geloescht, der Tag bleibt.

Wichtig fuer die Einordnung: upstream hat am 10. September offiziell `v4.35t`
veroeffentlicht, mit unserem PR #1135 (Punkte 104-210) darin. Der Buchstabe
`t` unterscheidet den Fork damit nicht mehr von der offiziellen Firmware; das
tut nur noch der Flash-Stempel in `--info` (`20260912` hier, `20260909` im
offiziellen 4.35t).

### Was dazugekommen ist

Ausloeser der Punkte 212-216 war eine Parser-Drift-Analyse
(`docs/archive/aprs-parser-drift-20260911.md`): die Firmware emittiert 17
`/X=`-Positionsschluessel, ihr eigener Decoder kannte nur 14.

- **Decoder liest `/R=`, `/U=`, `/I=`** (Punkt 212, `decodeAPRSPOS()` in
  `src/aprs_functions.cpp`) -- Gruppenliste, INA226-Busspannung und -Strom
  wurden bisher beim Empfang stillschweigend verworfen.
- **`/Y=`-Puffer-Reset** (Punkt 213, dieselbe Funktion) -- der Telemetriewert
  konnte Ziffern aus dem vorangehenden `/V=`-Scan erben.
- **Encoder-NaN-Guards pruefen jetzt den eigenen Puffer** (Punkt 214,
  `PositionToAPRS()` in `src/loop_functions.cpp`, neuer Header
  `src/pos_tag_nan.h`) -- sieben von acht Guards verglichen bisher `cpress`
  statt ihres eigenen Puffers, ein NaN in `/H= /T= /O= /F= /Q= /G= /C=` waere
  ungefiltert auf die Luft gegangen.
- **Contract-Dokumentation** (Punkt 215) -- `docs/architecture/11-wire-format.md`
  bekommt einen §1.8 mit dem vollstaendigen `/X=`-Schluesselraum, dazu
  Korrekturen in README und BACKLOG.
- **Decoder trennt `#name` vom Kommentar, `--setname` verbietet `#`** (Punkt 216,
  `b6d9f3cf`, N-35) -- `decodeAPRSPOS()` las den Kommentar bisher nur bis zum
  ersten `/` oder Leerzeichen und maximal 25 Byte; der vom Encoder angehaengte
  Node-Name (`#` + `node_name`) blieb im Kommentar kleben, ein Kommentar mit
  Leerzeichen wurde abgeschnitten. Jetzt endet der Bereich am ersten
  `/X=`-Token, der Text nach dem letzten `#` landet im neuen Feld `pos_name`.
  Damit der Split eindeutig ist, strippt `--setname` das Zeichen `#`.
  Dieselbe Regel ist parallel in MCProxy (`parse_aprs_position()`, `9501bb0`)
  und in der App (`parsePositionPayload()`, Branch `aprs-position-name`)
  umgesetzt; Ausloeser war ein Hinweis des App-Maintainers, dass der
  Name-Suffix nirgends gelesen wurde.

- **WebGUI behaelt die Gruppe im Zielfeld nach dem Senden** (Punkt 217,
  `86be10c8`). Der `sendmessage ok`-Handler leerte das Zielfeld nach jedem
  Senden; die naechste schnell getippte Nachricht ging als Broadcast an `*`.
  Gemeldet von DJ8MEH am 2026-09-11. Eine rein numerische Gruppe bleibt jetzt
  stehen, nur ein DM-Rufzeichen wird wie bisher geleert.

- **Langer Tastendruck schaltet den Knoten wieder aus** (DS-03, Punkt 218,
  `40c29e7f`). Seit Punkt 197 armieren die Deepsleep-Helfer die Taste als
  Wake-Quelle, `PressLong()` feuert aber waehrend die Taste noch gehalten wird
  -- die Wake-Bedingung war beim Einschlafen schon wahr, der Knoten bootete
  sofort neu (`RESET_REASON=5`). Neues `esp32WaitButtonRelease()` wartet auf
  das Loslassen (maximal 10 s, dann 100 ms Entprellung), in beiden Helfern und
  im Wireless-Paper/E213-Zweig, nur bei `--button on` (bei `--button off` ist
  der Pin nie konfiguriert und kann LOW floaten, RAK4631 WB_IO6 tut das).

- **T-Deck: Tastaturlicht bleibt bei aktivem Keylock dunkel** (TD-16, Punkt
  219, `9c3bdf6b`). `tft_on()` prueften `node_keyboardlock` statt der
  Tastaturlicht-Einstellung und setzte die Tastatur bei jedem Nachrichten-Wake
  des gesperrten Panels auf 150. Der Block ist weg, `setBrightness()` folgt
  bereits der echten Einstellung.

- **T-Beam Supreme bootet wieder auf 4.35t** (TM-09-Nachtrag, Punkt 220,
  `1427ac6f`). Punkt 203 gab dem u8g2-Konstruktor die I2C-Pins mit; damit
  macht u8g2 `pinMode(OUTPUT)` auf beiden Pins und trennt sie auf dem S3 vom
  I2C-Controller, den `Wire.begin(17, 18)` schon besitzt -- `u8g2->begin()`
  haengt. Zwei Knoten standen nach `Auto detecting display:`. Konstruktoren
  ohne Pins (Muster T-Beam v1.2 / RAK), Bustakt 100 kHz wie der Sensorpfad.

- **Upstream-Sync auf `1cb2d9e6`** (Punkt 221). Drei reine Merges: Kurts
  eigener Wechsel auf `4.35t` (PRs #1136/#1137, offizielles Release am 10.
  September), OE1KFRs "RAK LEDs off in Deepsleep" (PR #1139: gruene und blaue
  LED werden vor System OFF auf LOW gezogen), und upstreams Merge unseres PR
  #1140 mit den Punkten 218-220. `fork-main` ist damit inhaltsgleich mit
  upstream `dev` bis auf die Punkte 212-217, das Fork-eigene Safeboot (153,
  154, 186), `--port` in den Upload-Kommandos (151), die Host-Testumgebungen
  und `FLASH_VERSION`.

- **Punkt 200 (Wireless Paper / E213, Chip-Select-Hold) ist im Feld
  bestaetigt.** OE3LCR hat am 2026-09-11 auf E213 (OE3LCR-11) und Wireless
  Paper V1.2 (OE3LCR-10) den EXT1-Wake nachgestellt: `RESET_REASON=8`,
  `wake: 3`, SX1262-Init und SPI-Verkehr danach in Ordnung. Vor dem Fix liefen
  dort schon Timer-Wakes mit RX, der Aufruf ist also vermutlich praktisch
  wirkungslos und bleibt als Absicherung.

### Was fuer dieses Release auf Hardware geprueft wurde

- **Heltec V3 (DK5EN-93)**: `--deepsleep` ueber seriell schlaeft sofort, kein
  Reboot in 15 s; langer Tastendruck mit `--button on` zwei Zyklen -- dunkel,
  bleibt nach dem Loslassen dunkel, Wake beim naechsten Druck mit
  `RESET_REASON=8 DEEPSLEEP` (Punkt 218). Geflasht am 2026-09-12 mit dem
  Quellstand dieses Zyklus.
- **Heltec V3 Gateway (DK5EN-98)**: Punkt 217 nach WiFi-OTA am 2026-09-11 mit
  dem jsdom-Harness gegen den laufenden Knoten geprueft (Gruppe bleibt,
  DM-Rufzeichen wird geleert); der Test schlaegt auf der alten Firmware fehl.
- **T-Deck Plus (DK5EN-14)**: Bench-Szenario `keylock_kbl` auf einem
  Instrument-Build -- `[KBL];set;150` auf dem alten Code, kein Schreibzugriff
  auf dem neuen; Handtest 2026-09-12: KBL aus, SYM+K, LoRa-Nachricht weckt das
  Display, die Tastatur bleibt dunkel (Punkt 219). Das Geraet laeuft auf dem
  Instrument-Image, nicht auf dem Release-Image.
- **Wireless Paper V1.2 und Vision Master E213 (OE3LCR-10/-11, Feld)**: Punkt
  200, siehe oben -- upstream `dev` nach #1137, derselbe Codepfad, nicht
  dieses Image.

### Was ausdruecklich NICHT geprueft wurde

- Kein Board hat das veroeffentlichte Image selbst gesehen; die drei Boards
  oben liefen denselben Quellcode auf frueheren Commits dieses Zyklus.
- **APRS-Decoder (Punkte 212-216)**: nur die native Suite. Kein Board hat eine
  echte Position mit `/R= /U= /I=` oder einem `#name`-Kommentar empfangen,
  weder auf der seriellen Konsole noch in der WebGUI.
- **T-Beam Supreme (Punkt 220)**: nur kompiliert, kein Board hier. Die
  Feldbestaetigung mit dem Fix-Build durch die beiden Melder ist offen.
- **RAK4631 (DK5EN-90)**: der nRF52-Deepsleep-Pfad hat sich geaendert (Punkt
  218 Warte-auf-Loslassen mit `--button`-Gate, Punkt 221 LEDs aus) und wurde
  nicht erneut gefahren; der Knoten steht in System OFF und braucht erst die
  Reset-Taste. Heltec T114 und T-Echo nur kompiliert.
- **T-Beam v1.2 (DK5EN-92)**: keine Bench-Zeit in diesem Zyklus.
- Alles, was in `v4.35t.09.10` und `v4.35s.09.09` unter "NICHT geprueft"
  steht, gilt weiter.

---

## Stability-Release v4.35t.09.10 (2026-09-10)

Eine Aenderung des Forks gegenueber `v4.35s.09.09`, Changelog-Punkt 211:
Gleichstand mit upstream `dev` und ein eigener Versionsbuchstabe.
`FLASH_VERSION` geht auf 20260910, `FLASH_STRUCT_VERSION` bleibt unveraendert
20260724 -- die Einstellungen der Knoten bleiben erhalten. Gates: 656
native Testfaelle in 12 Host-Umgebungen, alle 32 Release-Umgebungen gebaut.
Bench in diesem Zyklus: keine.

### Was dazugekommen ist

- **Upstream-Sync auf `674413ce` (Punkt 211).** Upstream `dev` ist seit
  unserer Merge-Base `4e649eae` um fuenfzehn Commits weiter; vierzehn davon
  sind das KISS/TCP-Interface aus PR #1114 und dessen vollstaendiger Revert in
  PR #1128, netto null. Uebrig bleibt Kurts "v4.35s path + 2 chars" vom 3. September: `mheardPathBuffer1` waechst von 50 auf 52 Byte je Slot, der
  gespeicherte Pfadtext wird bei 51 statt 37 Zeichen gekappt, die
  `--path`-Tabelle ist zwei Spalten breiter, der T-Deck-Pfad-Tab formatiert in
  den groesseren Puffer. Per `git cherry-pick -x` unveraendert uebernommen,
  Autorschaft bleibt bei upstream. Alle Konsumenten des Puffers geprueft:
  WebGUI gibt per `%s` aus, Terminator an Index 51 steht immer, der
  T-Deck-Zeilenpuffer hat 60 Byte. Nebenwirkung auf dem T-Deck: `/mhpath.dat`
  auf der SD wird je Slot zwei Byte groesser, der Groessen-Check in
  `loadPathPersistence()` loescht die alte Datei einmalig -- ein
  Heard-Path-Cache, keine Einstellung, upstream verhaelt sich identisch.

- **Versionsbuchstabe `t` (Punkt 211).** `SOURCE_VERSION_SUB` und
  `SOURCE_VERSION_WEB_SUB` gehen von `s` auf `t`. Ein Knoten mit dieser
  Firmware meldet sich in `--info`, auf der Luft und in der Fleet-Firmware-Sicht
  als `4.35t` und ist damit ohne Blick auf den Flash-Stempel vom offiziellen
  `4.35s` zu unterscheiden. Upstream hat kein 4.35t veroeffentlicht; sollte das
  passieren, folgt der Buchstabe wieder upstream, wie am 3. September beim
  Sprung p -> s.

### Was fuer dieses Release auf Hardware geprueft wurde

Nichts. Kein Board wurde vor der Veroeffentlichung mit diesem Image geflasht.
Der Nachweis ist der Build aller 32 Umgebungen und die native Test-Suite; die
Bench-Nachweise fuer die Punkte 104-210 stehen in den Abschnitten der
vorherigen Releases und gelten unveraendert, weil sich an diesem Code nichts
geaendert hat.

### Was ausdruecklich NICHT geprueft wurde

- `--path` auf der seriellen Konsole und der Pfad-Tab auf dem T-Deck mit dem
  breiteren Puffer -- die Aenderung ist upstreams, nicht unsere, und rein
  mechanisch.
- Der einmalige Verwurf von `/mhpath.dat` auf dem T-Deck nach dem Update.
- Alles, was in `v4.35s.09.09` unter "NICHT geprueft" steht, gilt weiter.

---

## Stability-Release v4.35s.09.09 (2026-09-09)

Zwei Aenderungen des Forks gegenueber `v4.35s.09.06.2`, Changelog-Punkte 209
und 210. `FLASH_VERSION` geht auf 20260909, `FLASH_STRUCT_VERSION` bleibt
unveraendert 20260724 -- die Einstellungen der Knoten bleiben erhalten. Gates:
656 native Testfaelle in 12 Host-Umgebungen, alle 32 Release-Umgebungen
gebaut. Bench in diesem Zyklus: DK5EN-98 (Heltec V3, Gateway).

### Was dazugekommen ist

- **Track sagt jetzt, was es die Zelle kostet (TRK-01, Punkt 209).** Track
  (SmartBeaconing) drueckt die Beacon-Kadenz einer einzelnen Station auf bis
  zu 10 s herunter, gegen einen Kanal, der im Mittel etwa ein Paket alle 8 s
  traegt. Eine Handvoll Track-Nutzer reicht damit aus, eine Zelle fuer alle
  anderen zuzufahren -- genau das Bild, das aus dem Feld als Paketverlust
  gemeldet wird. Bisher wies keine Bedienoberflaeche darauf hin: die WebGUI
  beschrieb den Schalter als "enable display of SmartBeaconing", das serielle
  `--track on` quittierte kommentarlos, und ein Knoten, der mit gesetztem Flag
  bootet, schwieg dazu dauerhaft. Der vom Operator festgelegte Wortlaut lautet
  jetzt an drei Stellen `degraded MeshCom RX performance, packetloss likely`:
  im Moment des Einschaltens, einmal beim Boot solange das Flag steht, und in
  der WebGUI neben dem Schalter. Auf der seriellen Konsole traegt die Zeile das
  Praefix `Track on - `, weil dort sonst nichts verraet, welche Einstellung sie
  ausgeloest hat; in der WebGUI steht sie neben dem Schalter und bleibt nackt.
  An der Beacon-Kadenz selbst aendert sich nichts -- das ist eine Warnung, keine
  Drossel.

- **Einzelbyte-Umlaute ueberleben den Textfilter (CHR-03, Punkt 210).** Die
  reine UTF-8-Allowlist aus CHR-01/CHR-02 hat Legacy-Umlaute bauartbedingt
  geloescht: Latin-1 schreibt sie als Einzelbyte, UTF-8 als Zwei-Byte-Sequenz,
  und ein alleinstehendes Legacy-Byte ist kein gueltiges UTF-8. Sender, die
  genau das tun, gibt es im Netz -- PinPoint ist einer --, entsprechend kam
  "Gruesse" als "Gre" an und eine Nachricht aus drei Umlauten als leere
  Nachricht. Neue Regel: ein Byte, das nicht Teil einer gueltigen
  UTF-8-Sequenz ist, wird als Legacy-Einzelbyte gelesen und unveraendert
  durchgereicht. Der erste Schnitt liess `0xA0-0xFF` zu, der Nachzug den
  gesamten Bereich `0x80-0xFF`, weil die Sender hier CP1252 verwenden -- dort
  liegen Euro-Zeichen, typografische Anfuehrungszeichen und Gedankenstriche.
  Transkodiert wird nichts: der Filter entfernt weiterhin nur Bytes, schreibt
  keins um und verbreitert keins, womit der In-Place-Kontrakt jedes Aufrufers
  und jedes Wire-Budget unberuehrt bleiben. Bewusst akzeptierte Folge: die
  Ausgabe ist nicht mehr garantiert valides UTF-8, die Deutung liegt bei der
  Gegenstelle.

### Was fuer dieses Release auf Hardware geprueft wurde

- **DK5EN-98 (Heltec V3, Gateway), ueber WLAN-OTA mit dem Code dieses Releases
  geflasht.** Punkt 209 vollstaendig in der WebGUI durchgespielt: Hinweis
  verborgen bei Track aus, erscheint beim Einschalt-Klick in Rot neben dem
  Schalter ohne Reload, ist nach einem vollstaendigen Seiten-Neuladen bei
  aktivem Track weiterhin sichtbar, und verschwindet beim Ausschalten wieder.
  Der Knoten hat ueber `/getparam/?track` bei jedem Schritt denselben Zustand
  gemeldet und wurde so hinterlassen, wie er vorgefunden wurde (`track=off`).
  Die serielle Haelfte wurde aus dem laufenden meshlogger-Mitschnitt auf
  `rpizero` gelesen -- die Netconsole auf 2323 ist Einzelplatz und genau dieser
  Mitschnitt haelt den Platz --, die Zeile steht dort wortgleich. Ein
  isoliertes `--track on` erzeugt genau eine Zeile, drei Renderings der
  Setup-Seite erzeugen keine.

### Was ausdruecklich NICHT geprueft wurde

- **Punkt 210 hat keine Hardware-Verifikation.** Der Nachweis sind 27
  Filter-Testfaelle und ein Ende-zu-Ende-Fall, der ein Latin-1-Byte, eine
  UTF-8-Sequenz und das CP1252-Euro-Byte durch `encodeAPRS()` und
  `decodeAPRS()` zurueckholt; Orakel ist die FCS-Pruefung des Decoders. **Eine
  echte PinPoint-Nachricht wurde mit diesem Build nicht empfangen.**
- **Die Boot-Zeile aus Punkt 209 wurde auf Hardware nicht ausgeloest.** Sie ist
  im gelinkten Image beider Plattformen als String belegt, aber es wurde kein
  Knoten mit gesetztem Track-Flag neu gestartet.
- **Die nRF52-Haelfte von Punkt 209 wurde nicht auf Hardware geprueft.** Nur
  der Build und der String im Image; DK5EN-90 hatte in diesem Zyklus keine
  Bench-Zeit.
- **Folgen von Punkt 210 fuer Verbraucher auf dem EXTUDP-Seitenband sind nicht
  durchgemessen.** Ein Empfaenger mit striktem UTF-8-Decode verwirft jetzt
  Frames, die er vorher angenommen hat, weil die Firmware die Bytes bislang
  fuer ihn geloescht hat. mc-chat kann es, andere Verbraucher wurden nicht
  geprueft.
- **Offen und nicht Teil dieses Releases:** die Flotte hat keine Telemetrie
  darueber, wie viele Knoten tatsaechlich mit Track laufen. Ausserdem zeigt der
  Mitschnitt von DK5EN-98 zwei `Track on`-Zeilen, die keiner Bedienung von uns
  zuzuordnen sind -- irgendetwas im LAN hat einem Produktions-Gateway zweimal
  `--track on` geschickt. Emitter und WebGUI sind als Ursache ausgeschlossen,
  der Rest ist offen.

---

## Stability-Release v4.35s.09.06.2 (2026-09-06)

Zweiter Schnitt am selben Tag. Sieben Aenderungen des Forks gegenueber
`v4.35s.09.06`, Changelog-Punkte 202 bis 208. `FLASH_VERSION` bleibt
20260906, `FLASH_STRUCT_VERSION` unveraendert 20260724 -- die Einstellungen
der Knoten bleiben erhalten. Gates: 643 native Testfaelle in 12
Host-Umgebungen, alle 32 Release-Umgebungen gebaut. Bench in diesem Zyklus:
DK5EN-93 (Heltec V3), DK5EN-98 (Heltec V3, Gateway), DK5EN-14 (T-Deck Plus),
DK5EN-90 (RAK4631), DK5EN-92 (T-Beam v1.2).

Das GitHub-Release `v4.35s.09.06` vom Vormittag wird durch dieses ersetzt;
der Git-Tag bleibt als Nachweis stehen. Die Release-Notes sind fuer diesen
Schnitt umgeschrieben und beschreiben jetzt den **gesamten Abstand zum
offiziellen upstream `4.35s`** (Punkte 104 bis 208), nicht mehr nur den
Zuwachs gegenueber dem Vorgaenger-Release.

### Was dazugekommen ist

- **Die barometrische Hoehe ist nicht mehr 0, bis jemand `--setpress` tippt
  (GPS-08, Punkt 204).** `fBasePress` wurde ausschliesslich von diesem
  Handler geschrieben und nicht persistiert, also lieferte jeder Knoten mit
  BMx280 nach jedem Neustart `ALT asl: 0 m`. `getPressALTf()` latcht den
  Referenzdruck jetzt selbst aus dem aktuellen QFE, sobald die Referenzhoehe
  existiert -- gesetzt von `baroBaseRelatch()` bei Konvergenz des
  GPS-Filters, auf Knoten ohne GPS von `getPressASL()`. Bewusst nicht
  persistiert: ein neues Settings-Feld wuerde `FLASH_STRUCT_VERSION` heben
  und die Flotte loeschen, und ein alter Druckwert nach langer Standzeit ist
  schlechter als gar keiner. Bench DK5EN-93: Kaltstart ohne Kommando,
  Referenz nach 312 s scharf, barometrische Hoehe 474 m.

- **Der GPS-Hoehenfilter setzt sich nicht mehr alle paar Minuten auf
  Ausreisser neu (GPS-07, Punkt 205).** Mit Gate 15 m und 10 Rejects hat er
  sich auf einem stehenden Heltec V3 in 1,9 h zwanzigmal neu gesetzt, jedes
  Mal genau auf den Ausreisser, der das ausgeloest hat -- der Filter hat
  damit nichts geliefert (sd 17,16 m gegen roh 16,93 m). Jetzt Gate 30 m und
  60 Rejects, also 3 min bei der ESP32-Kadenz. Erst offline auf beiden Korpora
  validiert (jede Aenderung allein setzt noch 5x neu, das Paar 0x), dann auf
  der Bench: 0 Re-Seeds in 2 h. Host-Test `test_feldserie93_keine_reseeds`
  spielt die Aufzeichnung ab und faellt mit den alten Konstanten durch.

- **Boards mit BMx280 fusionieren Barometer und GPS zur gemeldeten Hoehe
  (GPS-05b/GPS-09, Punkt 206).** Neues Arduino-freies Modul
  `alt_fusion.cpp`: GPS-Tiefpass plus Barometer-Hochpass, einpolig, tau
  30 min, Barometer in float (GPS-09: die Rundung auf ganze Meter hat die
  Kurzzeitstabilitaet um den Faktor zwei gekostet). Nur aktiv, solange die
  Druckreferenz scharf ist; jedes andere Board und die erste Minute nach dem
  Boot behalten den reinen Kalman-Wert. Bench, 2 h stehend, DK5EN-93:
  gemeldete Hoehe sd **6,6 m** (roh 12,2 m, Kalman allein 9,1 m, vorherige
  Firmware 17,2 m auf derselben Bench), Allan-Deviation bei 30 s **0,22 m**
  (vorher 4,87 m). Das Entwurfsziel sd <= 4 m wurde auf diesem Lauf **nicht**
  erreicht: der GPS-Mittelwert selbst ist in einer Stunde um 30 m gewandert,
  und ein 30-Minuten-Tiefpass folgt dem. Ein Offline-Replay derselben
  Aufzeichnung ergibt sd 3,6 m bei tau 2 h und 2,4 m bei tau 4 h. Tau ist
  eine Konstante (`ALT_FUSION_TAU_MS`); die Wahl bleibt ausdruecklich offen.

- **Die Eingaenge von Port A des MCP23017 gehen auf die Luft: `/D=` im
  Positions-Beacon und im Digital-Slot des APRS-`T#`-Frames (Punkt 207,
  upstream Issue 1076).** Acht Zeichen, GPA0 zuerst, GPA7 zuletzt
  (APRS-BITS-Reihenfolge), als OUTPUT konfigurierte Pins lesen `0`, Port B
  wird nicht gesendet. Ein Arduino-freier Formatierer, `src/mcp17_bits.h`,
  bedient beide Stellen. `/D` wurde gewaehlt, weil `/I=` schon den
  INA226-Strom traegt. Knoten ohne den Baustein erzeugen bytegleiche Frames,
  und die `BITS.`-Definitionszeile bleibt unangetastet. Die Empfangsseite
  parst `/D=` nach `aprsPosition.din`, und das EXTUDP-`tele`-Datagramm
  bekommt einen optionalen Schluessel `din` -- MCProxy, Web-App und mcmap
  koennen ihn ohne Aenderung auf ihrer Seite auswerten. Gate: 19 neue native
  Faelle in drei Suiten. Bench DK5EN-93 ueber `--injectraw`; der Sendepfad
  ist nur nativ bewiesen, ein MCP23017 liegt hier nicht.

- **Die Bench-Instrumentierung ist nicht mehr in einem normalen Board-Build
  (INS-01, Punkt 202).** `INSTRUMENT_ENABLED` stand auf jeder ESP32- und
  nRF52-Umgebung auf 1, und `printfdeb()` haengt nicht an `--debug`: Knoten
  im Feld haben ungefragt `[INSTR-LOOP];gap;...` auf die serielle Konsole
  geschrieben, und Nutzer haben das als Fehlermeldung gemeldet. Der Default
  ist jetzt 0; eine Messfirmware baut man mit
  `PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1" pio run -e <env>`.

- **Zwei Korrekturen an genau dieser Umstellung, gefunden beim Schneiden
  dieses Releases (ebenfalls Punkt 202).** Erstens waren vier Schalter
  Kollateralschaden und sind keine Bench-Geruest-Kommandos: `--udplog`,
  `--udpstat` und `--wifistat` auf ESP32 sowie `--ethstat` und `--udplog`
  auf nRF52 sind das, was ein Gateway-Betreiber braucht, um den
  UDP-/WLAN-/Ethernet-Pfad im Feld mitzuschreiben. Ohne sie liess sich
  `bUDPLOG` nie setzen, also waren die `[UDP];rx/tx`-Zeilen pro Datagramm und
  die `[GW];rx;type;DATA`-Zeile in einem ausgelieferten Build unerreichbar.
  Sie stehen jetzt in einem eigenen Abschnitt vor dem `#if`-Block und werden
  in `--help` wieder angekuendigt. Zweitens hat `--help` weiterhin
  `--injectmsg`, `--injectraw`, `--loratx` und die T-Deck-UI-Hooks
  aufgezaehlt, obwohl die Handler wegkompiliert sind; diese Hilfezeilen
  stehen jetzt im selben `#if INSTRUMENT_ENABLED`.

- **T-Beam Supreme: das OLED blockiert die Hauptschleife nicht mehr rund
  570 ms (TM-09 fuer `BOARD_TBEAM_V3`, Punkt 203).** Das Display lief noch
  auf U8g2-Software-I2C mit Ein-Seiten-Puffer, also acht komplette
  Neuzeichnungen und acht bitgebangte Uebertragungen pro Bild -- im Feld als
  `[INSTR-LOOP];gap;ms;577;in;display_tick` gemessen, viermal pro Minute.
  Jetzt Hardware-I2C mit Vollbildpuffer, auf `Wire` und nicht `Wire1`, weil
  die Supreme das OLED am selben Bus wie PMU, RTC und Sensoren hat. Nicht auf
  Supreme-Hardware bestaetigt -- kein solches Board hier.

- **Der Gateway-Flood-Injektor der Bench sendet nur noch unser eigenes
  Rufzeichen (Punkt 208).** `tools/bench/experiments/gwflood.py` hat
  aufgezeichnete Korpus-Frames woertlich wiederholt, also trug jeder
  injizierte Frame einen fremden Source-Path, und das Gateway unter Test hat
  ihn unter einer Lizenz ausgestrahlt, die wir nicht haben -- 15 solche
  Aussendungen am 2026-08-30. Der Injektor baut seine Frames jetzt aus dem
  Wire-Layout mit fest auf `DK5EN-93` gesetztem Source-Path; der Mock-Server
  erzwingt es zusaetzlich (`assert_own_source_path()` in `send_gate()`).
  Reines Bench-Werkzeug, keine Firmware-Datei betroffen.

### Was wieder herausgeflogen ist

- **CTY-02 (upstream Issue #1133) ist zurueckgenommen.** Der Gateway-Fix war
  geschrieben und auf beiden Plattformen auf Hardware bewiesen (DK5EN-93 und
  DK5EN-90), dann wurde das Issue widerlegt. Der Revert `de18bcf0` setzt den
  Quelltext exakt auf den Stand davor zurueck; in diesem Release steckt
  nichts davon.

### Was fuer dieses Release auf Hardware geprueft wurde

- **DK5EN-93 (Heltec V3, Bench):** Kaltstart ohne Kommando, Druckreferenz
  nach 312 s scharf, barometrische Hoehe 474 m (Punkt 204). Zwei Stunden
  stehend: 0 Re-Seeds des Hoehenfilters (Punkt 205), gemeldete Hoehe sd
  6,6 m, Allan-Deviation 0,22 m bei 30 s (Punkt 206). Korpus-Frame f003 mit
  eingesetztem `/D=01001100` und neu gerechneter FCS ueber `--injectraw` bis
  zum EXTUDP-Listener als `"din":"01001100"` (Punkt 207).
- **Die vier wiederhergestellten Diagnoseschalter auf einem Serien-Build
  (Punkt 202), auf allen vier Bench-Boards nach dem Flashen des
  Release-Images geprueft:**
  - DK5EN-98 (Heltec V3, Gateway, WLAN-OTA): `--wifistat` liefert
    `[WIFI];stat;up;rssi;-42;...`, `--udpstat` liefert `[UDPSTAT];bind;1990;...`,
    `--udplog on` -> `[UDP];log;1` mit anschliessenden `[UDP];tx;...`-Zeilen
    pro Datagramm, `--udplog off` -> `[UDP];log;0`. `--injectraw` und
    `--heap` antworten `...wrong command`, wie beabsichtigt.
  - DK5EN-90 (RAK4631, DFU): `--ethstat` liefert beide `[ETH];stat`-Zeilen,
    `--udplog on/off` -> `[UDP];log;1` / `[UDP];log;0`. `--wifistat`,
    `--injectraw` und `--heap` antworten `...wrong command` -- `--wifistat`
    gibt es auf nRF52 korrekterweise nicht.
  - DK5EN-14 (T-Deck Plus, USB): `--wifistat`, `--udpstat`, `--udplog on/off`
    alle vier mit Marker; `--injectraw`, `--spitrace on` und `--heap`
    `...wrong command`.
  - DK5EN-92 (T-Beam v1.2, esptool mit 460800 Baud): `--wifistat`,
    `--udpstat`, `--udplog on/off` mit Marker; `--injectraw` und `--heap`
    `...wrong command`. Radio-Init `SX1276 Chip Initializing ... success`.
- **String-Scan der gebauten Images:** `INSTR-LOOP`, `--injectraw` und
  `--injectmsg` fehlen im Serien-Build, `--udplog`/`--wifistat`/`--udpstat`
  sind vorhanden; auf dem nRF52-ELF `--ethstat` und `--udplog` vorhanden,
  `--wifistat` erwartungsgemaess nicht.
- **Gates:** 643 native Testfaelle in 12 Host-Umgebungen gruen, alle 32
  Release-Umgebungen gebaut.

### Was ausdruecklich NICHT geprueft wurde

- **Der Sendepfad von `/D=` (Punkt 207).** Es liegt kein MCP23017 auf der
  Bench; nur die Empfangsseite und der EXTUDP-Schluessel sind auf Hardware
  bewiesen, der Rest nativ.
- **Punkt 203 auf einer echten T-Beam Supreme.** Kein solches Board hier;
  die Umstellung ist nur uebersetzt und gegen die bereits erledigte,
  gleichartige Aenderung fuer Heltec V3/V4 gespiegelt.
- **Bewegte Knoten in der Hoehenfusion.** TRACK umgeht beide Filter, und
  eine TRACK-Aufzeichnung mit Druck wurde nicht gemacht.
- **Tau der Fusion.** 30 min ist gesetzt, 2 h und 4 h sind nur im Replay
  gerechnet, nicht auf der Bench gefahren.
- **Alles, was schon fuer `v4.35s.09.06` nicht geprueft war**, bleibt
  ungeprueft; die dortigen Abschnitte gelten unveraendert weiter.

---

## Stability-Release v4.35s.09.06 (2026-09-06)

Zehn Aenderungen des Forks gegenueber `v4.35s.09.05`, Changelog-Punkte 192
bis 201. `FLASH_VERSION` 20260906, `FLASH_STRUCT_VERSION` unveraendert
20260724 -- die Einstellungen der Knoten bleiben erhalten. Gates: 616 native
Testfaelle in 12 Host-Umgebungen, alle 32 Release-Umgebungen gebaut. Bench in
diesem Zyklus: DK5EN-14 (T-Deck Plus), DK5EN-90 (RAK4631), Heltec V3 (Bench
und DK5EN-98 als Gateway des Betreibers).

### Was dazugekommen ist

- **`--deepsleep` schlaeft auf jedem ESP32-Board wirklich (DS-01, Punkt 197,
  upstream Issue #962 Option A).** Bisher haben nur Wireless Paper und Vision
  Master E213 eine Aufwachquelle scharf gemacht; jedes andere ESP32-Board fiel
  mit LoRa-Chip im RX, leuchtendem Display und (T-Beam-Familie) laufenden
  PMU-Schienen in `esp_deep_sleep_start()` und kam nur per RESET zurueck.
  Gemeinsamer Einstieg `esp32EnterDeepSleep()` (`src/esp32/esp32_sleep.cpp`):
  Funk schlafen, Display aus, AXP192/AXP2101-Schienen fuer LoRa und GPS aus,
  GPS- und ADC-Pins aus, dann ext1-Wakeup auf dem konfigurierten Taster-Pin
  (`--button` wird beachtet), `RTC_PERIPH` bleibt an, damit der Pull-up den
  Schlaf ueberlebt. T-Deck/T-Deck Plus trennen TFT und die gemeinsame
  LoRa/GPS/Tastatur-Schiene, T-Beam-1W den Funk-LDO; der tote
  Schienen-aus-Zweig des Wireless Tracker (prueft ein Makro, das keine
  Variante definiert) ist repariert, der Wireless Stick in der
  Vext-aus-Liste ergaenzt.
- **`--deepsleep` auf RAK4631, Heltec T114 und T-Echo ist ein echtes System
  OFF (Punkt 198, Issue #962 Abschnitt 6).** Auf dem RAK4631 war der Befehl
  ein No-op, auf dem T114 ein Soft-off-Umschalter, den ein zweiter Aufruf
  wieder zuruecknahm, waehrend BLE, Funk und USB weiterliefen, auf dem T-Echo
  nicht umgesetzt. `nrf52EnterDeepSleep()` (`src/nrf52/nrf52_sleep.cpp`),
  auch hinter dem T-Echo-Langdruck: `Radio.Sleep()`, Advertising aus, Display
  aus, Peripherie-Schiene getrennt, Serial/Wire/SPI abgebaut, dann
  `systemOff()` des Adafruit-Cores. Aufwachen per Taster, USB-Anstecken oder
  RESET. Zwei Review-Funde vor dem Commit: `Serial1.end()` nur, wenn der Port
  je gestartet war (der Core haengt sonst endlos, und der GPS-Baudscan beendet
  den Port auf T114/T-Echo ohne GPS), `systemOff()`-Pin gegen `PINS_COUNT`
  geprueft.
- **T-Deck, T-Deck Plus und T-Beam-1W kommen mit funktionierenden Schienen
  und Funk aus dem Schlaf zurueck (Punkt 199).** `gpio_hold_en()` ueberlebt
  den Aufwach-Reset, nichts hat den Hold geloest: `TDECK_POWERON` und
  `RADIO_LDO_EN` blieben nach dem Tasten-Wakeup auf LOW, SD-Karte, Tastatur
  und LoRa starteten bis zum Stromwegnehmen nicht mehr, die
  Hintergrundbeleuchtung flackerte (GPIO42 liegt ausserhalb des RTC-Bereichs
  und brauchte `gpio_deep_sleep_hold_en()`). Alle Holds werden beim Boot vor
  der Neukonfiguration geloest, nach dem Muster des T5-E-Paper. Auf der
  DK5EN-14-Bench gefunden.
- **Wireless Paper / Vision Master E213: der Chip-Select-Hold des SX1262 wird
  beim Aufwachen geloest (DS-02, Punkt 200).** Gleicher Mechanismus wie
  Punkt 199: `PIN_LORA_NSS` blieb nach dem ersten Schlaf/Wakeup auf HIGH,
  Funk tot bis zum Stromwegnehmen. `gpio_hold_dis()` vor der Funk-Init, beim
  Kaltstart wirkungslos. Zunaechst blind repariert (nur kompiliert), am
  2026-09-11 von OE3LCR auf E213 und Wireless Paper V1.2 im Feld bestaetigt
  (EXT1-Wake, `wake: 3`, Funk-Init und SPI danach in Ordnung). Vor dem Fix
  liefen dort laut OE3LCR schon Timer-Wakes mit RX, der Aufruf ist also
  vermutlich praktisch wirkungslos und bleibt als Absicherung.
- **ESP32-S3 an nativem USB: Port-Oeffnen waehrend des Boots fuehrt nicht
  mehr in eine Reboot-Schleife (CDC-02, Punkt 201).** Der 4-kB-Ring aus
  Punkt 184 wurde nach `Serial.begin()` angelegt, ein paar Zeilen lang war
  der Ring NULL, waehrend der CDC-Interrupt schon lief:
  `assert failed: xRingbufferReceiveUpToFromISR ringbuf.c:1269`, dann zwei
  bis drei Reboots bis zum PANIC. Der Ring wird vor dem ersten `begin()`
  dimensioniert, Einmal-Flag fuer das zweite `begin()` auf T5-E-Paper und
  T-Deck Pro. Bench DK5EN-14: 9 von 30 Port-Oeffnungen stuerzten vorher ab,
  0 von 80 nachher.
- **ACK-Statusframes an die App tragen das Rufzeichen der quittierenden
  Station (Punkt 192)**, Node-ACK den letzten Hop, Peer-ACK den Partner;
  ohne bekanntes Rufzeichen ist der Frame byteidentisch zu vorher. Session-
  Flag `--ackinfo on` (McApp setzt es beim Verbinden) hebt die
  Nur-der-Erste-Sperren fuer eigene msg_ids auf. **Ein Gateway meldet der App
  keine Heard- und Gateway-ACK-Frames mehr fuer Nachrichten, die es nur vom
  Server weitergereicht hat (ACK-01, Punkt 193)**; McApp buchte vorher rund
  150 falsche `send_success`-Zeilen pro Tag.
- **Web-GUI: die Nachrichten-Tabs werden gruen und zeigen eine Anzahl, wenn
  ungelesene Nachrichten eingetroffen sind (WEB-05, Punkt 195).** Knotenseite
  ein Attribut je Nachricht (`data-ts`, die Einfuegezeit im Ring), der Rest
  im Browser: Id-Menge fuer die Sitzung plus je Tab eine gespeicherte
  Wasserlinie (localStorage), beim ersten Besuch gesetzt, damit kein Wall
  aus Badges erscheint. "All" gilt als alles gelesen, im versteckten Fenster
  bleibt ungelesen, eigene Nachrichten und ACK-Aktualisierungen zaehlen nie.
  Nur auf der Nachrichten-Seite, Poll-Takt unveraendert. **`{CET}`-Zeitbaken
  erscheinen nicht mehr in der Liste (WEB-06, Punkt 196)** -- ein Gateway
  legt sie fuer die Uhr der App in den Ring, und sie haetten bei jeder Bake
  die Tabs `*` und All gruen gemacht.
- **Web-GUI: die QRS-Marke im LoRa-Queue-Panel zeigt die Tiefe, ab der die
  naechsten eigenen Nachrichten wirklich QRS ausloesen (WQ-02, Punkt 194).**

### Deepsleep: was drin ist, was nicht, wo wir Hilfe brauchen

Issue #962 fragt, warum der Akku-Deepsleep nichts tut. Befund
(`docs/archive/issue-962-deepsleep-verdict.md`): er tut nicht falsch, er ist seit
`e0043a56` (4.35p.07.11, wegen Issue #1053) fuer jedes Board komplett
abgeschaltet, und der manuelle `--deepsleep` hat auch nie richtig
geschlafen. Dieses Release repariert den manuellen Befehl auf jedem Board
(Punkte 197 bis 200).

Ausdruecklich **nicht** enthalten:

- **Keine automatische Abschaltung bei niedriger Spannung.** Option B aus
  dem Befund (Timer-Wakeup-Schleife mit Hysterese, opt-in) ist entworfen,
  nicht umgesetzt. Der alte Waechter bleibt auskommentiert, wie upstream ihn
  hinterlassen hat.
- **Kein Lightsleep.** Option C (Lightsleep mit LoRa-Wakeup) wird im Befund
  abgeraten und wurde nicht angefasst.
- **Keine Schlafstrom-Messungen.** Es gibt hier kein Labornetzteil, keine
  entladenen Akkus und kein Strommessgeraet in der Akkuleitung. Schwelle,
  Hysterese, Aufwachschleife und die 1053-Regression aus der Bench-Matrix
  koennen hier nicht gefahren werden; die Zahlen "einige zehn Mikroampere"
  und "etwa 2 Mikroampere" sind Datenblatt- und Core-Werte, keine Messungen.
  Eine Abschaltung, die selbst entscheidet, wann ein Knoten dunkel wird,
  ohne sie testen zu koennen, liefern wir nicht aus.

**Wir sind auf die Community angewiesen.** Wer eines der folgenden Boards
hat: bitte mit `--deepsleep` schlafen legen, aufwecken, und die
Reset-Grund-Zeile des Bootlogs sowie den Zustand von LoRa, Display und GPS
danach in den Thread zu Issue 962 oder als Issue in dieses Repository
stellen:

- Wireless Paper, Vision Master E213 (Punkt 200 ist blind): zwei
  aufeinanderfolgende `--deepsleep`/Tasten-Wakeup-Zyklen, danach empfaengt
  `--mheard` noch.
- Vision Master E290: das E-Ink behaelt im Schlaf das letzte Bild, kein
  Geraet zum Pruefen.
- T-Beam Supreme (AXP2101-Schiene, nur kompiliert), T-Beam v1.2/SX1262/
  SX1268 (AXP192-Schienen), T-Beam-1W (Funk-LDO).
- Heltec T114 und T-Echo (System OFF nur kompiliert; der RAK4631 ist das
  einzige nRF52-Board auf der Bench).
- E22-DevKitC, E22_XML-DevKitC, ttgo-lora32-v21: der Taster-Pin hat dort
  keinen externen Pull-up, die Firmware haelt den internen im Schlaf am
  Leben -- auf diesen Boards nicht getestet.
- Wer ein Messgeraet hat: Schlafstrom in der Akkuleitung, auf jedem Board.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Umgebungen gebaut, 616 native Testfaelle in 12
  Host-Umgebungen gruen.
- T-Deck Plus (DK5EN-14): `--deepsleep` und Tasten-Wakeup mit dem
  gpio-Hold-Fix, Schienen und Funk danach in Ordnung (Punkte 197, 199);
  Port-Oeffnen-Schleife 9 von 30 vorher, 0 von 80 nachher (Punkt 201).
- RAK4631 (DK5EN-90): System OFF und Wakeup (Punkt 198); Harness-Lauf auf
  dem Release-Stand: Boot, Ethernet, LoRa RX/TX, MHeard unauffaellig.
- Heltec V3 (Bench, und DK5EN-98 als Gateway): `--deepsleep` und
  Tasten-Wakeup (Punkt 197); ACK-Attribution gegen McApp in der Luft (Punkt
  192); Tab-Badges per jsdom-Harness gegen den laufenden Knoten, 30
  Pruefungen (Punkt 195); `{CET}`-Filter im Browser gesehen (Punkt 196).

### Was ausdruecklich NICHT geprueft wurde

- Punkt 200 (Wireless Paper / E213) ist blind: nur kompiliert.
- Deepsleep auf T-Beam Supreme, T-Beam v1.2-Familie, T-Beam-1W, Heltec
  T114, T-Echo, Vision Master E290 und den E22-DevKitC-Boards: nur
  kompiliert. Auf keinem Board wurde Schlafstrom gemessen.
- Abschaltung bei niedriger Spannung und Lightsleep: nicht umgesetzt, nicht
  geprueft.
- Die QRS-Vorhersage (Punkt 194) wurde nicht gegen einen echten Burst im
  Browser angesehen; die Getter sind durch fuenf native Faelle festgenagelt.
- Alles unter "Known gaps" in `release-notes.md`, unveraendert aus den
  Vorreleases: Safeboot-Gate ohne 4-MB-Bench-Arm, Ring-Flood-Fall der
  Echo-Sperre, `--postime 0`, GPS-07, `--setlog` ohne Hardware-Lauf, GPS
  A/B/C-Arme, T-Deck-Pin-Fallback, CONF-Koordinaten, 2S-Nullpunkt und
  INA226-Zweig, QRS-Drei-Nachrichten-Regel.

## Stability-Release v4.35s.09.05 (2026-09-05)

Ab diesem Release traegt der Tag kein `-stability`-Suffix mehr; die Linie und
ihre Regeln bleiben gleich. Dreizehn Aenderungen des Forks gegenueber
`v4.35s.09.03-stability`, Changelog-Punkte 179 bis 191. `FLASH_VERSION`
20260905, `FLASH_STRUCT_VERSION` unveraendert 20260724 -- die Einstellungen
der Knoten bleiben erhalten. Gates: 591 native Testfaelle in 12
Host-Umgebungen, alle 32 Release-Umgebungen gebaut.

### Was dazugekommen ist

- **Echo-Sperre fuer die eigene Back-Pressure-Formulierung (BP-11, Punkt
  179).** Ein angeschlossener Client (App, oder ein zweiter Knoten ueber
  EXTUDP) konnte die `QRT NOT SENT - <text>`-Quittung des Knotens als neue
  Nachricht zurueckreichen; der Knoten lehnte erneut ab, stellte erneut das
  Praefix voran, und nach dem Leerlaufen des Rings ging der Stapel in die
  Luft. Feldbeleg: fuenf Praefixe tief, neun Frames in drei Sekunden. Der
  Knoten erkennt seine eigene Formulierung beim Einreichen exakt
  (`bpIsOwnWording()`, `src/backpressure.h`) und verwirft sie still. Die
  Veroeffentlichung war vom 04.09. bis 05.09. auf Betreiberentscheidung
  zurueckgehalten und wurde fuer dieses Release freigegeben. Beleg in
  `docs/archive/node-msg.md`.
- **RAK4631 sendet nach Flash-Reset wieder mit 22 dBm (Punkt 180, upstream
  #1132).** Seit 4.35p ist die Markierung "Leistung nicht gesetzt" -20 statt
  0; der nRF52-Bootpfad hat sie nie normalisiert, `getPower()` klemmte -20
  auf 2 dBm, die App zeigte -20. `resolve_tx_power()` in
  `src/settings_sanitize` bildet beide Markierungen auf die Board-Vorgabe
  ab; `nrf52setup()` wendet sie vor der Funkkonfiguration an,
  `sendNodeSetting()` meldet den wirksamen Wert. Native Test mit sechs
  Faellen.
- **T-Deck, alle mit Bench-Beleg auf DK5EN-14 (Punkte 181 bis 185):**
  Trackball-Taste liefert einen LVGL-Click pro Druck statt zwei (TD-13);
  Send und Save Setting wechseln ohne Animation zurueck auf den
  Nachrichten-Tab, die halb abgebrochene Animation liess den Bildschirm auf
  einem geteilten Frame stehen (TD-12); der Karten-Tab komponiert die
  SD-Karte einmal statt zweimal, 718 ms statt 1337 ms Loop-Luecke (TD-14);
  das `msg_roll`-Szenario belegt, dass der Nachrichten-Tab keine Totzeit
  hat, die gemessenen Totzeiten sind SD-Karten-Rekompositionen (TD-14,
  TD-09).
- **ESP32-S3 an nativem USB blockiert die Hauptschleife nicht mehr, wenn der
  USB-Host weg ist (CDC-01, Punkt 184).** Der HWCDC des Arduino-Cores setzt
  nach dem ersten Host-Read ein 100-ms-Timeout und senkt es nie wieder; nach
  dem Ziehen des Kabels blockierte jeder Print, der nicht in den
  256-Byte-Ring passte. Timeout 0 (verwerfen statt warten), 4-kB-Ring.
  Beleg: 7 Luecken bis 1,8 s in 44 s ohne, keine mit der Aenderung.
- **Safeboot-OTA schliesst fail-closed ab (TM-49, Punkt 186).** Ein Upload,
  dessen Verbindung vor dem letzten Frame abbrach, erreichte den
  Abschluss-Handler ohne Fehlerflag und schaltete die Boot-Partition auf ein
  halb geschriebenes Image um. `_ota_image_valid` wird an genau einer Stelle
  gesetzt, nach `Update.end(true)` mit MD5-Pruefung; beide Handler koppeln
  Callback, Reboot und HTTP 200 daran, sonst 400 `incomplete_upload` und
  Rueckfall auf den 180-s-Timer des Safeboot.
- **Extern-UDP `tele`-Datagramm relayter Knoten (TLM-04, Punkt 187):** `qfe`
  trug die `/F=`-Druckhoehe in Metern statt des `/P=`-Stationsdrucks.
  `qfe` ist jetzt der Druck, die Hoehe steht unter dem neuen Schluessel
  `pressure_alt` (nur lora-Form). Builder in `src/extern_tele_json.h`,
  nativ getestet.
- **Analogeingang mit ungesetztem GPIO (ADC-01, Punkt 188):** kein
  `Pin 99 is not ADC pin!`-Sturm mehr, Abtastung pausiert, `--info` und
  Web-Status sagen `GPIO not set, measurement paused`. Die gespeicherte
  Einstellung wird nie hinter dem Betreiber geaendert.
- **Web-GUI (Punkte 189, 190):** Queue-Panel auf der RX-Log-Seite (TX-Ring
  je Prioritaet, Back-Pressure-Zustand, Dedup-Fenster, letztes
  STAT-Fenster; Flash +7 kB, RAM unveraendert). Die Nachrichten-Seite liest
  den ganzen BLE-Ring statt des Telefon-Fensters -- mit verbundener App war
  sie bisher immer leer --, haelt die Historie im Browser (Limit 200) und
  filtert ueber Tabs (Alle, `*`, je Gruppe, DM).
- **Werkzeug (MEM-03, Punkt 191):** der Speicher-Waechter prueft jetzt
  `iram0_0_seg` und `dram0_0_seg` aus der Map-Datei; bisher sah er nur DRAM
  und die PSRAM-inklusive Summenzeile, deshalb blieb der IRAM-Ueberlauf von
  PR #1114 unentdeckt.

### Was fuer dieses Release auf Hardware geprueft wurde

- **T-Deck Plus (DK5EN-14, Bench):** TD-13 5 von 5 Clicks korrekt mit, 5 von
  5 falsch ohne die Aenderung; TD-14 `map_tab_pick` ein Rebuild 294x182,
  718 ms; CDC-01 Loop-Luecken-Zaehler ueber den Port-Open-Reset getragen;
  TD-12 Tab-Wechsel nach Send/Save friert nicht mehr.
- **RAK4631 (DK5EN-90, Bench, Build `dfc02801`):** vor der Aenderung
  `TXPWR 2 dBm`, nach `--cleanflash` und Reboot `RF_POWER: 22 dBm` im
  Bootlog und `TXPWR 22 dBm` in `--info`; Sweep `--txpower 15/10/5/2` exakt
  zurueckgelesen, `1`, `0`, `-20` abgelehnt, gespeicherte 2 dBm ueberleben
  den Reboot.
- **Heltec V3 (DK5EN-98, Bench und seit 04.09. als Gateway im Betrieb):**
  Echo-Sperre vier von fuenf Bench-Faellen.
- **Bauen:** alle 32 Release-Umgebungen, 591 native Testfaelle gruen.

### Was ausdruecklich NICHT geprueft wurde

- Der Ring-Flut-Fall der Echo-Sperre (fuenfter Bench-Fall) auf DK5EN-14.
- Der fail-closed-Pfad des Safeboot auf einem 4-MB-Board mit einem
  abgebrochenen Upload.
- Die Web-GUI-Aenderungen als gemessener Bench-Arm; sie wurden waehrend der
  Entwicklung interaktiv benutzt, der Zehn-Minuten-Ringueberlauf aus dem
  Plan ist nicht gelaufen.
- T-Beam v1.2 und alle nicht genannten Boards: nur gebaut.
- Alle aus 09.03 uebernommenen offenen Punkte (GPS-07, `--setlog` auf
  Hardware, GPS-Vergleichsarme, `--postime 0`, TD-15, MEM-04-Risiko).

---

## Stability-Release v4.35s.09.03-stability (2026-09-03)

Wartungs-Release. Der **gesamte Firmware-Unterschied zu
`v4.35p.09.02-stability` ist upstreams eigener `4.35s`-Schnitt**
(`c908a4dd`, ueber PR #1126 in `dev`): zwei Zeilen Versionskennung und eine
Korrektur an `--postime`. Aus diesem Fork ist in diesem Schnitt keine einzige
Code-Zeile dazugekommen -- die Aenderungen des Forks seit dem letzten Release
sind Dokumentation und Werkzeug-Metadaten.

### Was upstream geaendert hat

- **`--postime` hat bisher jeden brauchbaren Wert verworfen.** Der Befehl hob
  Werte unter 300 s auf 300 s an und setzte im `else`-Zweig jeden Wert **ab**
  300 s auf `0`, was auf den einkompilierten Vorgabewert durchfiel. Damit war
  ueber `--postime` gar kein Intervall einstellbar. Upstream hat den
  `else`-Zweig entfernt; ein Wert ab 300 s wird jetzt uebernommen und wird zu
  `posinfo_interval`. (Changelog-Punkt 177.)
- **Versionskennung `p` -> `s`**: `SOURCE_VERSION_SUB` und
  `SOURCE_VERSION_WEB_SUB` melden `4.35s` in Banner, `--info`, Weboberflaeche
  und Positionsbake. (Changelog-Punkt 178.)

### Was der Fork dazu geaendert hat

Nichts an der Firmware. Organisatorisch: der Arbeitsbranch hiess
`v4.35p_prio` und war damit nach dem Versionsbuchstaben von upstream benannt
-- er veraltete bei jedem upstream-Schnitt. Er ist auf den versionsneutralen
Namen `fork-main` umgestellt, der alte Remote-Branch ist geloescht, der letzte
`4.35p`-Stand liegt unter dem Tag `archive/v4.35p_prio-20260903`. Das
Tag-Schema der Release-Prozedur liest die Version jetzt aus
`configuration_global.h`, statt `4.35p` fest verdrahtet zu haben.

### Bekannte Einschraenkung, bewusst uebernommen

`--postime 0` schaltet die periodische Positionsbake nicht mehr ab: `0` ist
kleiner als 300 und wird jetzt auf 300 s angehoben. Vorher war der Befehl in
die andere Richtung kaputt. Der Fork patcht das nicht selbst, damit die beiden
Baeume nicht auseinanderlaufen -- das gehoert als eigener PR nach upstream.

### Was fuer dieses Release auf Hardware geprueft wurde

Nichts. Es gab in diesem Schnitt keinen Hardware-Lauf. Das ist vertretbar,
weil der Quelltext ausserhalb der beiden upstream-Hunks bytegleich zu
`v4.35p.09.02-stability` ist -- dessen Bank- und Feldergebnisse (T-Deck Plus
DK5EN-14, OE5HWN T-Deck Plus und T-Beam Supreme je eine Stunde GPS) gelten
unveraendert weiter.

### Was ausdruecklich NICHT geprueft wurde

- **Der `--postime`-Fix selbst.** Er ist gelesen und verstanden, aber auf
  keinem Knoten gegen eine tatsaechlich veraenderte Bakenperiode gemessen.
- Alle offenen Punkte aus `v4.35p.09.02-stability` bleiben offen: der
  `--setlog`-Banklauf (RAK-90 + Heltec-93, 30 min), die GPS-Zwei-Stunden-Arme
  A/B/C, der T-Deck-Pin-Fallback auf Hardware, TM-49 (Safeboot-OTA-Abschluss
  nach Abriss), das BLE-`blelen + 2`-Ueberlaufrisiko.
- **GPS-07** (neu im Backlog, Nachtaufzeichnung 2026-09-03): der
  Kalman-Filter fuer die Hoehe setzt sich nachts zehnmal auf rohe Ausreisser
  neu auf -- die Re-Seed-Schranke ist zu klein bemessen. In der Wiedergabe
  behoben durch `RESEED_N 60` bzw. ein 30-m-Gate; beides ist in diesem
  Release **nicht** enthalten.

---

## Stability-Release v4.35p.09.02-stability (2026-09-02)

Drei Teilsysteme aus parallelen Worktrees, jedes einzeln reviewt
(`/fable-review`, Finder-Faecher plus adversarialer Verifizierer) und heute
Abend in dieser Reihenfolge gemerged: `--setlog on`-Instrumentierung
(Item 173), GPS-Hoehe/QNH-Pfad (Item 174), T-Deck Tasten-Auto-Repeat
(Item 175). Dazu Code Quality 2.0 mit Detektoren und der Reset-Grund im
ESP32-Bootbanner (170-172) und der Upstream-`dev`-Sync #1124 (176).
`FLASH_VERSION` steigt auf 20260902. Native-Gate 568/568 in 12 Host-Envs,
102 Tool-Tests.

### Was dazugekommen ist

- **GPS-Pfad (GPS-01..04, Feldmeldung OE5HWN-14).** Die GPS-UART wird in
  jedem Schleifendurchlauf geleert statt alle 3 s; vorher gingen ~165 Byte
  NMEA je Zyklus verloren und etwa jeder 256. zusammengesetzte Satz kam als
  "Fix" mit `lon:0.000000` oder `Date: 2015.14.00` durch die Pruefsumme.
  Ein Plausibilitaetsgatter verwirft Null-Insel, unmoegliche Kalenderdaten
  und Uhrzeiten, Hoehen ausserhalb -500..10000 m, bevor sie Settings,
  Beacon oder Systemuhr erreichen. Die gebeaconte Hoehe ist ein
  Kalman-Schaetzwert (Zeitkonstante ~7 min, dt-skaliertes Prozessrauschen
  fuer den 1-s-Takt auf nRF52), TRACK-Modus umgeht ihn; `--setalt` seedet
  den Filter und lehnt Werte ausserhalb 0..40000 m ab statt auf 0 zu
  klemmen. Die QNH-Referenzhoehe wird bei Konvergenz und bei
  `--setalt`/`--setpress` nachgezogen, nicht mehr auf den ersten Fix nach
  dem Boot festgenagelt. Feldlogs OE5HWN (T-Deck Plus, T-Beam Supreme, je
  eine Stunde): 2375 Auswertungen ohne eine korrupte Stichprobe,
  Konvergenz nach 88 Stichproben wie modelliert, QNH-Relatch nach Reboot.
- **GPS-06: T-Deck ohne Plus, Pin-Fallback.** Seit Upstream `a672d18b`
  (v4.35p) empfaengt der T-Deck auf GPIO44 und sendet auf GPIO43 (LilyGo-
  und Plus-Belegung); bis 4.35d war es per `SoftwareSerial(43, 44)`
  umgekehrt, selbst verdrahtete Module nach alter Belegung waren seither
  stumm (Feldlog: 4.35d Fix mit 7 Satelliten, 4.35p "keine gueltige
  NMEA-Sequenz auf 8 Baudraten"). `detectBaudrate()` scannt bei Fehlschlag
  einmal auf der alten Belegung, merkt sich die wirksamen Pins fuer alle
  spaeteren `begin()` und bittet im Log ums Umverdrahten. Nur
  `variants/t_deck` definiert den Fallback.
- **`--setlog on`: sieben Zeilenarten (SL-01..SL-07).** RX-Zeile mit
  `RSSI:`/`SNR:`/`DUP:`/`OWN:`/`t=`, neu `RLY` (Relay-Entscheidung mit
  Grund), `TX` (eigener Sendevorgang mit Wartezeit, Ringtiefe,
  CAD-Versuchen), `ERR` (RX-Fehler auf beiden Plattformen), `STAT`
  (5-Minuten-Kanalauslastung, Dedup, Ring-Hochwasser, Drops, Heap) und
  `GWI`/`GWU` (Gateway-Inject/-Upload je `msg_id`). Alles haengt nur an
  `bDisplayLog`, unter 64 Byte RAM, keine neuen Puffer; `tools/berglog.py`
  liest die Zeilen (39 Tool-Tests). Review-Fixes S1-S9: ein einziger
  Dedup-Lookup je Frame, Gateway-`newid`, Hop-Maske 0x20, kein `String`
  je Zeile, gemeinsame STAT-Befuellung.
- **T-Deck Tasten-Auto-Repeat (TD-10).** Backspace, Leertaste und
  Buchstaben wiederholen sich beim Halten (400 ms, dann alle 100 ms) ueber
  LVGLs Keypad-Repeat; dafuer wird das Raw-Mode-Fenster der
  Tastatur-Controller-Firmware (I2C 0x03/0x04, 5-Byte-Matrix) fuer die Dauer
  des Haltens geoeffnet und nur scharf geschaltet, wenn der Frame genau die
  Matrixzelle der gedrueckten Taste zeigt. Controller ohne Raw-Mode
  (LilyGo-Firmware vor 2025-06-12) verhalten sich wie bisher; `--info`
  zeigt `...KBD raw-mode yes|no|unknown`. 39 native Faelle, Review-Fixes
  K1-K7.
- **Code Quality 2.0** (`docs/code-quality-2.0.md`, 28 Code- und 16
  Prozessmuster mit Detektoren `CQ2-*` in `tools/code_audit_scan.py`),
  ESP32-Bootbanner mit Reset-Grund (TM-51), zwei Fundstellen der Detektoren
  behoben (`PositionToAPRS()`-`strncat`-Grenze, SD-Map-Timer
  rollover-sicher).
- **Upstream #1124 (OE3LCR):** die ADC-Polaritaetsprobe erkennt einen fest
  anliegenden Spannungsteiler (Wireless Stick V3) als Batteriehardware.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 568/568 in 12 Host-Envs, 102
  Tool-Tests.
- Der gemergte Baum selbst hat nur die Gates gesehen. Alle
  Hardware-Befunde stammen vom Pre-Merge-Testbuild der GPS- und
  Tastatur-Branches (`test-helmut-gps-kbd-20260902`, `df9d407e`); Delta zu
  diesem Release: `--setlog`-Zeilen, T-Deck-Pin-Fallback, `--info`-Zeile,
  Upstream #1124.
- T-Deck Plus DK5EN-14 (Bank): GPS-Lauf 6,5 min, 128 Fixes, 0 Rejects,
  Konvergenz nach 256 s (Modell 249 s); Tastatur `support;1`,
  Haltefenster 0,8-1,2 s fuer Backspace, Leertaste und `d`, wiederholtes
  Loeschen am Display gesehen.
- T-Deck Plus und T-Beam Supreme OE5HWN (Feld, je eine Stunde): 1214 und
  1161 Auswertungen, 0 Rejects, 0 korrupt; Supreme konvergiert nach 88
  Stichproben bei 280 m, Relatch 276 m nach Reboot; QNH 1020,2 hPa gegen
  Flughafen Linz 1018,5 hPa, Rest passt zu einem BMP280-Offset, nicht zu
  einem Hoehenfehler. Die OE5HWN-Tastatur hat keinen Raw-Mode und tippt
  wie bisher.

### Was ausdruecklich NICHT geprueft wurde

- `--setlog on` hat keinen Hardware-Lauf (Welle 3, RAK4631 + Heltec V3,
  30 min, steht aus). Heltec V3, T-Beam und RAK4631 haben diesen Build
  nicht gesehen.
- Die GPS-Zwei-Stunden-Arme A/B/C sind nicht gelaufen; der Nachweis sind
  6,5 min Bank plus zwei Feldstunden.
- Der T-Deck-Pin-Fallback ist auf Hardware unverifiziert; der Build ging an
  OE5HWNs selbst verdrahtetes Geraet, das Ergebnis war vor dem Schnitt
  nicht da.
- Kein nRF52-GPS-Knoten auf der Bank (T114/T-Echo, 1-s-Takt).

## Stability-Release v4.35p.09.01.2-stability (2026-09-01, zweiter Schnitt)

Ersetzt v4.35p.09.01-stability vom Mittag (Release-Objekt geloescht, Tag
bleibt als Marker). `FLASH_VERSION` bleibt 20260901 (gleicher Tag, Praezedenz
08.27.2). Drei Dinge obendrauf: die QRS-Kalibrierung aus dem Feld (Item 167),
der ADC-Multiplikator des Heltec Wireless Stick V3 aus Upstream (Item 168)
und der Upstream-`dev`-Sync mit unserem PlatformIO-Upload-Fix (Item 169).
Native-Gate 489/489 in 12 Host-Envs.

### Was dazugekommen ist

- **QRS erst mit der dritten eigenen Nachricht auf vollem Ring
  (`QRS_MIN_USER_MSGS`).** Feldbefund vom Abend: "QRS -- slow down" war
  auch mit der BP-05-Linie bei Tiefe 5 viel zu empfindlich und kam schon
  bei der ersten Nachricht. Ursache: `txRingDepth()` zaehlt Relay-Frames,
  ACKs und Beacons mit, ein Gateway mit Grundlast 4 gibt dem Absender fuer
  seine erste Nachricht ein QRS fuer einen Stau, den er nicht gebaut hat.
  `onSend()` laeuft genau einmal pro lokal getippter Nachricht und nie fuer
  Relay/ACK/Beacon, also zaehlt die Zustandsmaschine dort ihre eigenen
  Aufrufe: QRS kommt, wenn drei eigene Nachrichten in Folge den Ring bei
  Tiefe >= 5 vorfinden. Faellt der Ring zwischendurch unter 5 (in `onSend()`
  oder im Drain-`poll()`), beginnt die Zaehlung von vorn -- wer so tippt,
  dass die Funke zwischendurch abarbeitet, ist nicht das Problem. QRT und
  QTA bleiben ungegatet, Latch/Hysterese/QRV-Hold unveraendert. Acht
  Zeilen in `src/backpressure.h`, `loop_functions.cpp` unangetastet. Vier
  neue Host-Faelle, sieben bestehende um zwei stille Vorlaeufer ergaenzt,
  der integrierte Burst-Test erwartet QRS jetzt bei Tiefe 7.
- **Heltec Wireless Stick V3 misst seinen Akku wieder** (Upstream PR #1119,
  OE3LCR, Issue #1116). `ADC_MULTIPLIER 4.9245` war vom Vision Master E213
  uebernommen; Referenzmessung ergibt 4.13. Mit dem alten Wert fiel die
  Messung aus dem Plausibilitaetsband von `battDetectUpdate()`, der Node
  meldete "keine Batteriehardware" und liess `/B=` weg.
- **PlatformIO-Upload ohne `upload_port` funktioniert, nRF52-Core-Warnungen
  begraben keine echten mehr** (Upstream PR #1118, von uns).
  `upload_protocol = custom` rief nie die Port-Erkennung auf, `$UPLOAD_PORT`
  blieb leer; mit `esptool` laeuft sie vorher und `upload_command` bleibt
  unser Befehl. `-Wall -Wextra` von `build_flags` nach `build_src_flags`
  (nRF52- und ESP32-Basis): in `build_flags` trafen sie den Adafruit-Core
  und die SoftDevice-Header, ueber 23.000 `-Wunused-parameter` pro
  Vollbuild. Upstreams `v4.35p compile` (#1117) nimmt die Flags aus den
  restlichen Varianten und das `--port` aus den T-Deck-Upload-Kommandos;
  beide Baeume sind synchron.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 489/489 in 12 Host-Envs
  (inkl. der vier neuen `QRS_MIN_USER_MSGS`-Faelle, DJ8MEH-Replay,
  Grundlast-Muster, Flood-Test 13-in-10).
- Heltec V3 dk5en-98 (Live-Gateway) laeuft diesen Build per OTA seit
  21:52 Uhr; die Web-GUI meldet den neuen Build-String, mehr wurde am
  Gateway nicht geprueft.
- Alles aus dem 09.01-Abschnitt darunter gilt fuer den 09.01-Build, der
  sich von diesem nur um eine Konstante in der Zustandsmaschine, den
  Wireless-Stick-Pin-Wert und Build-System-Aenderungen unterscheidet.

### Was ausdruecklich NICHT geprueft wurde

- Kein Burst gegen die neue Drei-Nachrichten-Regel auf Hardware; der
  einzige Feldbefund ist die Beobachtung "QRS bei der ersten Nachricht",
  die den Fix ausgeloest hat. T-Deck Plus, T-Beam und RAK4631 haben diesen
  Build nicht gesehen.
- Der Wireless-Stick-V3-Wert stammt aus OE3LCRs Referenzmessung, kein
  eigenes Board auf dem Tisch.
- Der originale DJ8MEH-Feldvorfall weiterhin nicht End-to-End auf
  Hardware nachprovoziert; TM-49 offen (4-MB-Boards bei marginalem Link
  per USB flashen); Batterie-Nullpunkt am 2S-Pack, INA226 und L76K-GPS
  unveraendert ungeprueft.

## Stability-Release v4.35p.09.01-stability (2026-09-01)

Ersetzt v4.35p.08.31.4-stability (Release-Objekt geloescht, Tag bleibt als
Marker). `FLASH_VERSION` steigt auf 20260901. Inhalt: der Endstand des
Backpressure-Themas -- BP-07 bis BP-10 schliessen die Luecken L1-L4, damit
ist das System komplett und wird ab jetzt als Ganzes berichtet, nicht mehr
in Zwischenschritten --, dazu TM-50 (meshlogger-Zombie-TCP) und der Rueckbau
zweier Reparaturen, deren Upstream-Feature nicht mehr existiert.
Changelog-Items 153-166 fassen im CHANGELOG-stability.md jetzt alles seit
v4.35p.08.31-stability konsolidiert zusammen (die Schnitt-Sektionen
.2/.3/.4 sind dort eingedampft, da keiner dieser Schnitte mehr als
GitHub-Release existiert).

### Was dazugekommen ist

- **BP-07 -- jede abgewiesene oder verworfene Nachricht bekommt eine
  Quittung mit ihrem Text.** Eigenes, nie latchendes Vokabular (`BpNack`),
  ein Rahmen pro verlorener Nachricht: `QRT NOT SENT - <text>` /
  `QTA NOT SENT - <text>`, auf 120 Bytes an UTF-8-Codepoint-Grenze
  gekuerzt, Anfuehrungszeichen/Backslashes/Steuerbytes vor dem
  JSON-Escaping bereinigt. Die Refuse-Pruefung laeuft jetzt NACH dem
  Dekodieren und dem Abstreifen des `{ZIEL}`-Praefixes -- die Quittung
  traegt den echten getippten Text, der ~60-Zeilen-Zweitparser
  (`bpPeekDst`) ist geloescht. Ein Ring-Drop gilt nur noch als
  Backpressure, wenn die Tiefe wirklich an der Refuse-Schwelle steht --
  ein fabrikneuer Knoten erzwingt keine falsche QRT-Episode mehr.
  `msg_id` aller BP-Rahmen aus einem gemeinsamen monotonen Zaehler
  (rolloverfest) statt `millis()`, sonst kollidieren die zwei Rahmen des
  Drop-Pfads im Dedup-Filter der App.
- **BP-08 -- ganz oder gar nicht.** Das App-Echo einer getippten Nachricht
  ging bisher raus, BEVOR der Ring gefragt war -- eine verworfene
  Nachricht sah im Chat wie gesendet aus, und auf einem Gateway lief der
  UDP-Uplink zum Server unabhaengig vom Ring-Ergebnis: die Nachricht
  erreichte den Server, ohne dass je ein RF-Nachbar sie gehoert haette.
  Jetzt schreibt der Ring zuerst; bei einem Drop kehrt die Funktion vor
  Echo, Buchhaltung und Gateway-Uplink zurueck, der Absender bekommt
  `QTA NOT SENT - <text>`.
- **BP-09 -- der getippte Text ueberlebt eine Abweisung.** `sendMessage()`
  liefert `BpSendResult` (0/-1/-2/-3) aus allen vier Signaturen; T-Deck
  und T-Deck Pro leeren das Eingabefeld nur bei Erfolg (der Tab-Wechsel
  bleibt, damit die Quittung sichtbar ist), die Web-API antwortet
  `sendmessage refused`/`dropped`/`invalid` statt pauschal `ok`, und das
  Web-GUI-JavaScript leert die Felder erst nach der Antwort -- es hatte
  denselben Textverlust eine Ebene hoeher noch einmal eingebaut.
- **BP-10 -- unabhaengige Advisor-Runde ueber BP-07/08/09**, drei echte
  Regressionen gefunden und behoben (T-Deck-Tab-Wechsel unterdrueckte die
  Quittung komplett; Ring-Drop wurde pauschal als Backpressure gelesen;
  nack-Marker loggte den unbereinigten Text), fuenf Medium-Fixes
  (u. a. 140-Byte-Puffer vom nRF52-Loop-Stack, `charset_utf8_safe_truncate()`
  statt Handgestricktem, nack latcht die Episode fuer das spaetere QRV).
  Alles im Endstand oben bereits enthalten.
- **TM-50 -- meshlogger erkennt Zombie-TCP.** Im Overnight-Soak toetete
  ein Router-Reboot die Node-Seite der 2323-Session ohne FIN; der Logger
  sass 2,4 h stumm auf recv()-Timeouts und konnte die Debug-Flags am Ende
  nicht restaurieren. Neu: Stille-Watchdog (`--stall-timeout`, Default
  90 s) in den bestehenden Reconnect-Pfad, SO_KEEPALIVE mit
  Plattform-Tunables, idempotentes Flag-Re-Apply nach jedem Reconnect,
  End-Restore mit einem Retry auf frischer Verbindung. Regressionstest
  gegen einen Fake-Konsolen-Server, fails-before verifiziert.
- **Rueckbau zu Upstream-Reverts.** Upstream hat das erweiterte
  Mheard-JSON (PP-Link-Kette, SRC/GW) und das String-`FWDATE` im
  BLE-I-Register wieder entfernt; dieser Baum zieht nach. Der tote
  PP/DIST-Fail-Soft-Block in `updateMheard()` ist raus, die
  FWDATE-Reparaturen sind mit dem Feld selbst gegenstandslos (das
  Register traegt nur noch `FWVER`). Es bleibt nur, was lebenden Code
  repariert: die HEY-Eingangsschranke und das puffer-begrenzte
  BLE-JSON-Framing.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 485/485 in 12 Host-Envs
  (inkl. DJ8MEH-Replay, Grundlast-Muster, Flood-Test 13-in-10).
- Der BP-07..10-Build lief auf allen vier Bench-Boards: T-Deck Plus
  (DK5EN-14, Quittung sichtbar, Text ueberlebt die Abweisung), Heltec V3
  als Live-Gateway (dk5en-98) und Bench-Node (dk5en-93), T-Beam v1.2
  (dk5en-92) und RAK4631 (dk5en-90).
- Overnight-Soak 2026-09-01 (98/90/93): Recovery sauber, 0 falsche
  BP-Marker im Normalbetrieb, 13 BP-03-Stale-Drops als Feldnachweis des
  Age-Out.

### Was ausdruecklich NICHT geprueft wurde

- Der originale DJ8MEH-Feldvorfall ist weiterhin nicht End-to-End auf
  Hardware nachprovoziert (nur nativ im Replay).
- TM-49 bleibt offen: 4-MB-Boards bei marginalem Link per USB statt OTA
  flashen.
- Batterie-Nullpunkt am echten 2S-Pack, INA226-Zweig und L76K-GPS
  unveraendert ungeprueft.

## Stability-Release v4.35p.08.31.4-stability (2026-08-31, vierter Schnitt)

Vierter Schnitt des Tages; v4.35p.08.31.3-stability bleibt bestehen.
`FLASH_VERSION` bleibt 20260831. Inhalt: die Notice-Politik der
Backpressure, an der Realitaet nachkalibriert (Changelog-Items 161-162),
beide Wellen mit Fable-Advisor-Gate.

### Was dazugekommen ist

- **BP-05 -- QRS ab Tiefe 5, QRV nur nach Refusal.** Eine 5,5-min-Messung
  auf dem Live-Gateway (DK5EN-98) zeigte Grundlast-Ringtiefe 1-4
  (Modalwert 2) und drei falsche QRS/QRV-Paare im voellig normalen
  Betrieb -- die alte QRS-Schwelle (Tiefe > 1) lag AUF der Grundlast,
  jede einzelne Nachricht loeste "slow down" aus. Neu: QRS ab Tiefe 5
  (fix auf allen Boards, defensiv unter der QRT-Schwelle geklemmt), das
  Band 2-4 ist still, und QRV kommt nur noch, wenn die Episode wirklich
  abgewiesen (QRT) oder verworfen (QTA) hat. Das aufgezeichnete
  Grundlast-Muster ist als Regressionstest festgenagelt.
- **BP-06 -- Notices landen im richtigen Chat.** Die Meldung zu einer
  Nachricht in Gruppe 20 kommt als Nachricht an `20` (erscheint im
  20er-Chat), bei einer DM im DM-Thread des Absenders -- nur fuer ihn
  sichtbar, die Rahmen gehen nie on air. Der Refuse-Pfad (lehnt vor dem
  Parsen ab) nutzt einen puren Ziel-Peek mit Paritaet zur echten
  Extraktion (11 Grenzfaelle nativ festgenagelt). Serial und T-Deck-GUI
  bleiben Klartext.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 480/480 in 12 Host-Envs
  (inkl. Feldmessungs-Grundlast als Regressionstest).
- Die Grundlast-Messung selbst stammt vom Live-Gateway DK5EN-98 mit dem
  .3-Build (5,5 min, tools/meshlogger.py ueber die 2323-Konsole).

### Was ausdruecklich NICHT geprueft wurde

- Die .4-Notice-Politik hatte zum Publish noch keine Bench-Zeit auf
  Hardware (Fleet-Flash folgt unmittelbar; erwartet: null [BP]-Marker im
  Normalbetrieb, Nachmessung ausstehend).
- Der originale DJ8MEH-Feldvorfall ist weiterhin nicht auf Hardware
  nachprovoziert; TM-49 bleibt offen (4-MB-Boards: bei marginalem Link
  USB statt OTA).

## Stability-Release v4.35p.08.31.3-stability (2026-08-31, dritter Schnitt)

Ersetzt v4.35p.08.31.2-stability (Release-Objekt geloescht, Tag bleibt als
Marker). `FLASH_VERSION` bleibt 20260831. Inhalt: die drei
Backpressure-RCA-Fixes aus dem DJ8MEH-Feldvorfall (8-Minuten-Refuse-Episode
auf Phantomtiefe) plus eine integrierte Regressions-Suite
(Changelog-Items 157-160). Jeder Fix lief als eigene Welle mit
unabhaengigem Fable-Advisor-Gate; Plan und beide Verdikte sind in
`docs/archive/bp-rca-fixes-*.md` archiviert.

### Was dazugekommen ist

- **BP-02 -- ehrliche Ringtiefe.** `txRingDepth()` und alle Marker
  (`RING_WRITE`/`RING_STATUS`/`TX_GATE_ENTER`/`TX_START`/`RING_TX_READ`)
  zaehlen belegte Slots statt der Indexdistanz `iWrite-iRead`, die
  freigegebene Loecher hinter einem ausgehungerten Eintrag am Lesezeiger
  mitzaehlte (Feldlog: `queued=19` bei real 3-4). `RING_STATUS` traegt die
  alte Distanz zusaetzlich als `dist=`; die RING_ZOMBIE-Detektoren in
  `tools/serial_monitor.py` und `tools/loganalyse.sh` sind portiert
  (queued-Fallback fuer alte Logs).
- **BP-03 -- Stale-HEY-Alterung.** Ein 2-s-Sweep im Main-Loop verwirft
  BACKGROUND-Eintraege (HEY, Prio 5) aelter als 3 min
  (`RING_DROP_STALE`) -- der Feld-Blocker sass 10 min und pinnte die
  Ringfront. Bewusst NICHT in `getNextTxSlot()` (laeuft auf nRF52 im
  Timer-Task); auf nRF52 Check+Drop pro Slot atomar unter dem Ring-Lock,
  EXT_PENDING ausgenommen, Rollover-sicher.
- **BP-04 -- QRV im Wasserband.** Tiefe 0 schliesst sofort wie bisher;
  Tiefe 1 schliesst nach 10 s ununterbrochenem Halt (explizites
  Armed-Flag, Zeit injiziert, Header bleibt Arduino-frei). Der
  Anti-Flap-Fall vom 08.30 ist als Regressionstest festgenagelt.
- **test_bp_regression** (native_aprs): der Vorfall Ende-zu-Ende am echten
  Ring plus echter Statemaschine -- Burst -> QRT -> Drain mit gepinntem
  Blocker -> QRV in 10 s (Feld: 8 min), Blocker-Alterungspfad,
  Gegenprobe QRT-Ausloesung. Jede der drei Fix-Regressionen einzeln per
  Mutation verifiziert.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen; Native-Gate 459/459 in 12 Host-Envs.
- Die Fleet (DK5EN-14/93/92/90 und Gateway DK5EN-98) laeuft diesen Build
  (98 per Safeboot-OTA mit Build-Stamp-Verifikation, 92 per esptool
  460800, 90 per DFU-Serial).

### Was ausdruecklich NICHT geprueft wurde

- Der originale Feldvorfall (DJ8MEH-Burst auf belastetem Kanal) wurde auf
  Hardware mit dem neuen Build noch nicht nachprovoziert -- der Nachweis
  lebt bisher im integrierten Native-Replay.
- TM-49 bleibt offen (Safeboot-Completion nach Disconnect, Risiko auf
  4-MB-Boards -- bei marginalem Link dort USB statt OTA).
- Die McApp-Darstellung der .2-Notice-Rahmung ist weiterhin nicht am
  Geraet bestaetigt.

## Stability-Release v4.35p.08.31.2-stability (2026-08-31, zweiter Schnitt)

Nachzuegler-Release am selben Tag, ersetzt v4.35p.08.31-stability
(Release-Objekt geloescht, Tag bleibt als Marker stehen). `FLASH_VERSION`
bleibt 20260831 -- rein informativ, keine Struktur-Aenderung. Inhalt: die
OTA-Tooling-Welle vom Nachmittag plus die Backpressure-Notice-Umrahmung
(Changelog-Items 153-156).

### Was dazugekommen ist

- **TM-46 -- Safeboot uebersteht einen abgebrochenen OTA-Upload.** Zentrales
  `abortActiveUpdate()` mit onDisconnect-Hook und Session-Generationszaehler;
  dazu die entscheidende Bench-Findung: ein Cross-Task-Race
  (unsigned-Unterlauf) im Stall-Watchdog konnte gesunde Uploads abbrechen
  (Fix: vorzeichenbehaftete Deltas + volatile). Abort-Retry zweimal auf
  DK5EN-14 bewiesen (Kill 5 s nach Upload-Start -> sofortiger Retry laeuft
  durch, kein `--force`).
- **TM-48 -- Safeboot-WLAN-Join nach Produktionsmuster** (Treiber-gewaehlter
  AP, PMF-off wie TM-34); `[SAFEBOOT];wifi;pmf_off;rc;0` belegt, ~170 kB/s am
  -74-dBm-Schreibtisch-Link.
- **TM-47 -- webflash.py:** Hardware-Strings behalten das `+` (`TDECK+`),
  Safeboot-Resume-Pfad, `--self-test`.
- **BP-01-Nachschliff -- Notices kommen als normale Nachricht vom eigenen
  Rufzeichen an.** BLE/Web: Absender ist das Node-Call statt "response"
  (landete in McApps Spam-Klasse 9999). EXTUDP: statt eigenem Typ "notice"
  jetzt exakt die Form einer ueber LoRa empfangenen Textnachricht (src_type
  "lora", type "msg", numerische Firmware-Version) -- der DJ8MEH-Vorfall vom
  Nachmittag (drei stumm verworfene Nachrichten, RCA aus dem 2323-Log) hatte
  gezeigt, dass keine Client-App die alte Form darstellt. Beide Rahmungen in
  testbare Header extrahiert (`bp_notice_frame.h`, `extern_notice_json.h`)
  und mit zwei neuen nativen Suiten festgenagelt (fails-before gegen beide
  Altverhalten verifiziert).

Die neuen Safeboot-Binaries (`safeboot.bin`, `safeboot-s3.bin`) liegen dem
Release bei; sie kommen nur per USB-Vollflash auf die Boards -- OTA tauscht
ausschliesslich das App-Image.

### Was fuer dieses Release auf Hardware geprueft wurde

- TM-46/47/48: Abort-Retry zweimal auf DK5EN-14 (T-Deck Plus), WLAN-Join-
  Marker und Durchsatz am Schreibtisch-Link; webflash-Runde ueber die Fleet
  (DK5EN-98 per OTA mit Build-Stamp-Verifikation).
- BP-01-Rahmung: nativ festgenagelt (445 Testfaelle, 12 Host-Envs); die
  Fleet (14/93/92/90/98) laeuft den Build mit dem Fix.

### Was ausdruecklich NICHT geprueft wurde

- Die McApp-Darstellung der neuen Notice-Rahmung nach dem Flash (der Test
  vom Nachmittag lief gegen die alte Firmware); Bestaetigung am Geraet steht
  aus.
- TM-49 bleibt offen: der ElegantOTA-Completion-Handler kann nach einem
  Disconnect `hasError==false` sehen und nach einem Teil-Write die
  Boot-Partition umschalten -- auf 16-MB-Boards durch Slot-Validierung
  harmlos, auf 4-MB-Single-Slot-Boards riskant. Guard + Bench-Beweis vor dem
  naechsten Release; bis dahin auf 4-MB-Boards bei marginalem Link USB statt
  OTA.
- Der Backpressure-Phantomtiefen-Befund aus der DJ8MEH-RCA (txRingDepth
  zaehlt freigegebene Loecher hinter einem ausgehungerten Prio-5-Eintrag,
  QRT-Episode dadurch minutenlang zu lang) ist analysiert, aber noch nicht
  gefixt.

## Stability-Release v4.35p.08.31-stability (2026-08-31)

Die komplette Feldkampagne seit dem 08.28-Hotfix: 46 nummerierte Aenderungen
(Changelog-Items 107-152), erarbeitet in zwei /orchestrate-waves-Durchgaengen
auf der Vier-Board-Bench (Heltec V3, T-Beam v1.2, T-Deck Plus, RAK4631 mit
W5100S-Ethernet), abgesichert mit 438 nativen Testfaellen in 12 Host-Envs und
Soak-Laeufen bis 9,1 h. Die vollstaendige Begruendung jeder Aenderung mit
Dateiverweisen und Messwerten steht im PR-Entwurf fuer upstream
(`docs/archive/pr-draft-20260831.md`); das englische Changelog
(`docs/CHANGELOG-stability.md`) traegt die nummerierte Liste.

### Die Schwerpunkte

- **Sicherheit:** Stored XSS in der Web-Nachrichtenseite (jeder LoRa-Absender
  konnte Script im Browser des Operators ausfuehren), drei Web-JSON-Endpunkte
  ohne Escaping, EXTUDP-Serialisierungs-Bounds, 13 BLE-Register-Builder auf
  einen fail-soften Rahmenpfad umgestellt, Guards gegen unkonfigurierte
  `XX0XXX`-Knoten (RX und TX), acht Parser-Befunde, Settings-Plausibilisierung
  nach dem Flash-Laden.
- **Zeit und Uhr:** NTP asynchron (kein 1-s-Blocker mehr) und -- der eigentliche
  Befund -- die NTP-Antwort wurde auf Nicht-Gateways nie gelesen: ein Knoten
  ohne Gateway und GPS bekam NIE eine gueltige Uhrzeit (9,1-h-Soak: 0 von 545
  Antworten geerntet). Dazu `--ntpsync` und MHeard-Alterung ueber die monotone
  Uhr statt der Wanduhr (der ewige NCNT-0-Fehler).
- **WLAN:** Erstverbindung an WPA2/WPA3-Uebergangs-APs (24/24 Fehlschlaege ->
  24/24 Erfolge), ereignisgetriebenes got_ip, asynchrones DNS, gestufter
  Watchdog.
- **Gateway:** Ein ESP32-Gateway relayte UDP-Positionsrahmen nie zu LoRa
  (Selbst-Dedup, 0/30 -> 30/30, beantwortet upstream #568). Und GW-01: das
  Gateway laedt seine eigene HEY nicht mehr selbst zum Server hoch -- die
  nackte Eigenkopie traf per Draht immer Sekunden vor den angereicherten
  Nachbar-Kopien derselben msg_id ein und konnte dort gewinnen; genau darum
  fehlten die Nachbardaten eines Gateways am Server, waehrend `--gateway off`
  sie zeigte. Gemessen am Interlink-Strom des Servers, vorher und nachher.
- **Rueckstau:** Eine in einen vollen TX-Ring getippte Nachricht verschwindet
  nicht mehr wortlos -- Q-Code-Meldungen (QRS/QRT/QTA/QRV) auf dem
  Herkunfts-Transport, nie ueber die Luft.
- **T-Deck:** Audio blockiert die Hauptschleife nicht mehr (~1,1 s pro
  Nachricht), die Karte pannt (i/j/k/l/o, g/h), Trackball per Interrupt,
  GT911-Retry, Boot 14,9 -> 10,9 s. Der Verlust des ersten TFT-Flushes nach
  SD-Zugriff ist instrumentiert und das geclobberte Register benannt:
  ausschliesslich `GPSPI2.clock` (via `--spitrace`); die NOP-Mitigation bleibt
  vorerst drin.
- **Bedienung:** `--maxhop`, Config-Export/-Import ueber die Web-API,
  `--wifi`/`--mute`/Persist-Kommandos seriell, `--display` steuert das
  T-Deck-Panel, `--help` mit 145 statt 95 Kommandos.

### Feld-Debugging ist Absicht

Dieses Release liefert die volle serielle Instrumentierung mit
(`INSTRUMENT_ENABLED=1`, `MC_INJECT_HOOKS=1`): kompakte `[TAG];key;value`-
Marker, zuschaltbar per `--debug`, `--udplog`, `--loradebug`, `--wifistat`,
`--instr` usw., abgreifbar ueber USB oder die Netz-Konsole auf TCP 2323
(`tools/meshlogger.py` fuer Langzeitmitschnitte). Dazu Injektions-Kommandos
(`--injectmsg`, `--injectpos`, `--injectraw` durch den echten RX-Pfad,
`--loratx`-Bursts, am T-Deck Tasten-/Trackball-/Touch-Injektion), damit sich
Feldbefunde ohne zweite Station reproduzieren lassen. Ein Knoten im Feld kann
damit Debug-Logs erzeugen, die wir anschliessend offline auswerten -- genau so
sind alle Befunde dieses Releases entstanden. Einzige Ausnahme: die RAM-
knappste Variante `E22_XML-DevKitC` (klassischer ESP32 plus SoftSerial/XML,
schon bisher ohne Netz-Konsole) baut Mitschnitt-Ring und Injektions-Hooks
nicht mit ein (`MC_CAPTURE=0`, `MC_INJECT_HOOKS=0`) -- die Kampagne hatte
diesen einen Link 648 Byte ueber das DRAM-Segment geschoben. Alle Marker und
Log-Schalter bleiben auch dort verfuegbar.

### Was fuer dieses Release auf Hardware geprueft wurde

- Alle 32 Release-Envs bauen sauber; 438 native Testfaelle in 12 Envs gruen.
- Heltec V3: WLAN-/NTP-/Batterie-/OLED-Pfade auf der Bench; GW-01-Fix-Build
  gegen den Live-Serverstrom verifiziert (nur noch angereicherte Kopien).
- T-Beam v1.2: 9,1-h-WLAN-Soak (55/55 Reconnects, 0 unerklaerte Disconnects),
  Beobachter-Gateway der GW-01-Messung.
- T-Deck Plus: komplette Harness-Regression (Boot, Display-CRC, Karte, Nav,
  Input, Heap, Trim, Touch-Injektion) PASS auf diesem Build; --injectraw und
  --loratx live nachgewiesen; `[SPITRACE];clobber`-Messung.
- RAK4631: ETH-01/CTY-01/NTP-Pfade auf der Bench, Bench-Gateway der Kampagne.

### Was ausdruecklich NICHT geprueft wurde

- T-Beam Supreme (L76K-GPS-Zweig ohne Test -- Bench-Module sind u-blox), alle
  E22-/LoRaAPRS-/Vision-Master-/T3-/T-Connect-/T-Echo-/T114-/Wireless-Paper-
  Varianten: bauen sauber, keine Bench-Zeit.
- Batterie-Nullpunkt an einem echten 2S-Pack, INA226-Zweig.
- CONF-Koordinaten werden geparst und geloggt, nicht angewandt.
- T5-E-Paper baut weiterhin nicht (vorbestehender Include-Pfad-Fehler,
  unabhaengig von diesen Aenderungen).

---

## Stability-Release v4.35p.08.28-stability (2026-08-28)

Ein Hotfix-Release. Drei Defekte im BLE-Rahmenpfad zum Telefon, einer davon
aktiv im Feld, dazu die Regressionstests.

### Das I-Register kam nicht mehr an

Upstream-Commit `82db3d41` hat den Wert von `FWDATE` im BLE-Register `I` von
der Ganzzahl `FLASH_VERSION` auf den String `__DATE__ " " __TIME__` umgestellt.
Das sind 14 Zeichen mehr, und damit ueberschreitet das Dokument die Grenze, die
die Firmware sich selbst setzt.

Die Grenze ist leicht zu uebersehen, weil die Zahl, gegen die der Registerbauer
prueft, nicht die ist, die greift: `command_functions.cpp` vergleicht gegen
`MAX_MSG_LEN_PHONE - 2` (298), waehrend die wirksame Klemmung eine Ebene tiefer
in `addBLEComToOutBuffer()` bei 245 Byte sitzt -- abzueglich der Typkennung also
244 Zeichen JSON. Darueber schneidet die Firmware mitten im Wert ab. Das
Ergebnis ist kein verkuerztes, sondern ein syntaktisch kaputtes Objekt: die App
verliert nicht das letzte Feld, sondern alle, also auch `CALL`, `ID` und `HWID`
und damit die gesamte Knotenidentitaet.

Auf dem eigenen Gateway gemessen: **59 verworfene Register in 9,5 Stunden**,
eines je Abgleichzyklus, ab dem Moment des Firmware-Starts. Nach dem Fix: null.

`FWDATE` traegt wieder `FLASH_VERSION`. Der Schluesselname bleibt, nur der Typ
wechselt von String auf Zahl -- und `FLASH_VERSION` ist der Wert, der pro
Release gepflegt wird, waehrend `__DATE__`/`__TIME__` die Uhrzeit des jeweiligen
Compilerlaufs ist.

**Nicht vollstaendig geloest:** ein Knoten mit allen sechs belegten
Gruppenruf-Slots liegt weiterhin 13 Zeichen darueber und war das auch schon vor
`82db3d41`. Die dauerhafte Loesung waere `GCB0..GCB5` als Array (spart 34-41
Zeichen), das ist aber eine fuer Apps sichtbare Kontraktaenderung und gehoert
abgestimmt statt still gemacht.

### Mheard-Datensaetze konnten spurlos verschwinden

Die `PP`-Linkkette im Mheard-JSON waechst je Relais um rund 11 Zeichen. Beim
Standard-Hopwert liegt der Datensatz bei ~214 Zeichen und faellt nicht auf.
`{SET}` laesst bis 7 Hops zu, und dort kippt der Pfad ohne jede Spur:
`addBLEOutBuffer()` klemmt `'D'`-Rahmen erst bei 255 statt bei 245, und die
Schreiblaenge wird in einem `uint8_t` gerechnet. Ab 253 Zeichen JSON wird aus
`blelen + 2` auf ESP32 eine 0 bzw. 1 -- der Rahmen geht als Null- oder
Ein-Byte-Schreibvorgang raus und ist weg, ohne Logzeile. nRF52 ist nicht
betroffen, dort wird derselbe Ausdruck nach `int` promoviert.

Der Bauer misst jetzt vor dem Serialisieren und opfert im Zweifel das teuerste
optionale Feld: erst `PP`, dann `DIST` (aus den Koordinaten beider Stationen
nachrechenbar). Ein Datensatz ohne `PP` ist weiter voll brauchbar, ein auf
Byteebene abgeschnittener ist unparsebar. Das Weglassen wird gemeldet.

Zusaetzlich ist die Kette an der Quelle begrenzt: `appendHeySignalReport()`
haengt nichts mehr an, sobald die naechste Gruppe `HEY_PATH_PAYLOAD_MAX`
sprengen wuerde. Der Wert ist so bemessen, dass die laengste REGULAERE Kette ihn
nie beruehrt -- ein von der Luft kommendes, ueberlanges `@`-Paket dagegen schon.

### Ausserdem

- Zwei benannte Konstanten ersetzen verstreute Zahlen: `BLE_JSON_PAYLOAD_MAX`
  (244) und `HEY_PATH_PAYLOAD_MAX`.
- Vier neue Faelle in `test_hey_report`. Zwei sind ohne den Fix rot, zwei sind
  Schutztests, die festhalten, was sich NICHT aendern darf: eine Kette ueber die
  volle Hoptiefe und der Fall genau an der Kante.
- Zwei Issue-Reports unter `docs/`, einer je Defekt, an die jeweiligen Autoren
  adressiert -- mit Rechnung, Code-Referenzen und Loesungsvorschlaegen.

### Was fuer dieses Release auf Hardware geprueft wurde

- **Heltec V3** (`heltec_wifi_lora_32_V3`) -- per WiFi-OTA auf einen Knoten im
  Live-Betrieb geflasht, Einstellungen unveraendert. Die verworfenen
  `I`-Register hoeren mit dem Neustart auf und sind ueber die folgenden
  Abgleichzyklen nicht wiedergekommen.
- **Alle 32 Release-Umgebungen** bauen sauber.
- **Native-Suiten** `native`, `native_aprs`, `native_dedup`, `native_capture`:
  93 Faelle gruen.

### Was ausdruecklich NICHT geprueft wurde

- Die BLE-Fixes sind nur auf Heltec V3 verifiziert. Der `uint8_t`-Ueberlauf
  betraf nRF52 ohnehin nie.
- Der Fall `PL >= 6` wurde nicht auf echter Hardware provoziert -- der Beleg ist
  die Rechnung plus der Fail-soft-Pfad, nicht ein Feldversuch.
- Ein Knoten mit allen sechs belegten Gruppenruf-Slots wurde nicht getestet;
  fuer den ist das `I`-Register weiterhin zu gross.
- Alles uebrige entspricht `v4.35p.08.27.2-stability`, inklusive der dort
  genannten Luecken.

---

## Stability-Release v4.35p.08.27.2-stability (2026-08-27)

Kein Entwicklungs-Release. Dieses Paket verschiebt die Basis des Forks von
`8114d7ae` (18. August) auf **`upstream/dev` vom 27. August (`fc83554e`)** --
also auf den Stand, **nachdem** Upstream unsere Aenderungen uebernommen hat.

### Der eigentliche Punkt

**Der gesamte Code-Anteil dieses Forks liegt jetzt im offiziellen MeshCom.**
Die ICSSW-Maintainer haben am 27. August beide eingereichten Pull Requests
gemergt:

- **PR #1102** -- 82 Stabilitaets- und Speichersicherheits-Aenderungen aus
  fuenf Monaten Feldbetrieb. 64 Dateien, +3.213/-1.113. Uebernommen als Squash
  `0cac4aea` (Merge `d4dee351`).
- **PR #1103** -- Puffergroesse im neuen FWDATE-Feld von Upstream selbst
  (`2d7f56b1`, Merge `fc83554e`).

Damit ist der Zweck dieses Forks erfuellt: die Verbesserungen sind nicht mehr
"unsere", sie stehen im offiziellen Baum und wirken auch ohne dieses Paket.

### Was aus dem Upstream dazukommt

Alles, was das ICSSW-Team und weitere Beitragende zwischen `8114d7ae` und
`fc83554e` ergaenzt haben -- 26 Dateien, +702/-4.021:

- **SD-Karten-Offlinekarten fuer das T-Deck Plus**, mit dynamischen Kartensaetzen,
  Zoom-Bereichserkennung pro Kartensatz, Auto-Zoom-Rueckfall bei fehlenden
  Kacheln, Nachladen an der Kachelgrenze und mehreren Marker-Korrekturen. Die
  fuenf einkompilierten Kartenblobs (Europa, Deutschland, Oesterreich, Wien,
  Wien-Umgebung -- rund 3.850 Zeilen generiertes C) entfallen zugunsten der
  Kacheln auf der Karte.
- **T-Echo BME280 repariert** -- richtige I2C-Pins, bedingte Adressbehandlung
  fuer BMP280 gegen BME280, ungenutzter zweiter I2C-Bus aus der Variante
  entfernt.
- **Mheard-JSON erweitert** -- die Linkkette der HEY-Bake traegt jetzt RSSI/SNR
  pro Hop, dazu Ursprungsrufzeichen und Gateway-Kennung. `serializeJson`
  bekommt die Puffergroesse statt einer gemessenen Laenge.
- **Info-JSON traegt das Build-Datum** (`FLASH_VERSION`).
- **Neues BLE-TYPE-`I`-Feld `FWDATE`**, damit Apps Sub-Releases unterscheiden
  koennen. `FWVER` bleibt unveraendert.

### Was von uns dazukommt

- **`FWDATE` wurde abgeschnitten.** Upstreams neues Feld legte `char cfwdate[20]`
  fuer `__DATE__ " " __TIME__` an -- noetig sind 21 Byte. `snprintf` kuerzte
  still die letzte Sekundenstelle. Gefunden vom `-Wformat-truncation`-Gate,
  behoben und als PR #1103 zurueckgegeben.
- **Bauhygiene:** ein Sondierungs-Flag im Batteriecode stand ausserhalb des
  `#if defined(ADC_CTRL_PIN)`-Blocks, der es benutzt -- auf Boards ohne diesen
  Pin eine ungenutzte Variable.

### Was sich auf dem Draht aendert

Gegenueber `v4.35p.08.27-stability` nichts. Die drei dort angekuendigten
Aenderungen (`/B=000`, unplausible ACK-Frames werden nicht weitergesendet,
korrigierte ESP32-Kanalauslastung) sind unveraendert und liegen jetzt auch im
offiziellen Upstream.

### Was fuer dieses Release auf Hardware geprueft wurde

Nichts Neues. Dies ist ein Rebase- und Verpackungs-Release; die Bench-Ergebnisse
hinter den eigenen Aenderungen sind die von `v4.35p.08.27-stability` und gelten
unveraendert weiter (OTA Heltec V3 mit erhaltener Konfiguration, OTA T-Beam v1.2,
DFU RAK4631 mit erhaltenem Rufzeichen und Ethernet-Konfiguration).

Geprueft wurde hier:

- Alle 32 Release-Umgebungen bauen sauber.
- Die sieben Haupt-Targets bauen mit `-Werror` auf `src/` ohne eine einzige
  Warnung aus eigenem Code.
- Der Baum ist gegenueber `v4.35p.08.27-stability` in allem ausser den oben
  gelisteten Upstream-Ergaenzungen unveraendert -- per Tree-Hash belegt, nicht
  per Durchsicht.

### Was ausdruecklich NICHT geprueft wurde

- **Upstreams neuer T-Deck- und T-Echo-Code hatte hier keine Bankzeit.** Weder
  ein T-Deck Plus mit SD-Karte noch ein T-Echo mit BME280 steht zur Verfuegung.
  Der Code baut sauber und wird mitgeliefert, aber niemand auf dieser Seite hat
  ihn laufen sehen.
- Alle Punkte aus `v4.35p.08.27-stability` bleiben offen: Batterie-Nullpunkt am
  echten Akku, INA226-Zweig, `--txcapture`-Sendeseite gegen einen zweiten
  Empfaenger, L76K-GPS-Module, Boot am Akku ohne USB-Host.

---

## Upstream-Sync 2026-08-27 (dev)

Upstream hat beide eingereichten PRs uebernommen. `upstream/dev` steht auf
`fc83554e`.

- **PR #1102** -- `Stabilitaets- und Speichersicherheits-Fixes aus dem
Feldbetrieb`: 82 Firmware-Aenderungen, 64 Dateien, +3.213/-1.113. Upstream
  hat sie als Squash `0cac4aea` gemergt (Merge-Commit `d4dee351`).
- **PR #1103** -- `fix(ble): FWDATE-Puffer zu klein`: `char cfwdate[20]` fasst
  `__DATE__ " " __TIME__` nicht (21 Byte noetig), `snprintf` schnitt die letzte
  Sekundenstelle ab. Ein Byte-Fix an Upstreams eigenem Code, gefunden von
  unserem `-Wformat-truncation`-Gate (Commit `2d7f56b1`, Merge `fc83554e`).

Damit liegt der gesamte Firmware-Anteil dieses Forks in `dev`.

**Integriert per Merge, nicht per Rebase.** `dev` traegt unsere 82 Commits als
einen einzigen Squash. Beim Replay der Einzel-Commits darauf bleiben nur
Residuen uebrig: Commits, deren Message einen Fix beschreibt, deren Diff aber
nur noch Loeschungen enthaelt -- verifiziert an
`fix: stack overflow in sendExtern()`, das zu 8 Loeschungen geschrumpft waere.
`git log -p` und `git blame` haetten ab dann falsche Auskunft gegeben. Der
Merge (`ee26403d`) erhaelt die Historie; der resultierende Baum ist
bit-identisch zum Stand davor (Tree `bc849071`), `dev` bringt nichts Neues mit.

Konflikte in 12 Dateien, alle zugunsten dieses Branches aufgeloest -- es sind
genau die Stellen, an denen der PR bewusst vom Fork abweicht:

| Datei                        | Abweichung                                          |
| ---------------------------- | --------------------------------------------------- |
| `platformio.ini`             | `[env:native*]`, `-Werror`, Safeboot-Framework-Hook |
| 10 Dateien unter `src/`      | Kommentare mit Verweisen auf `docs/` und `test/`    |
| `src/txring_functions.cpp`   | zusaetzlich der `NATIVE_BUILD`-Block                |
| `src/configuration_global.h` | `FLASH_VERSION 20260827` statt Upstream-Datum       |

Unsere uebernommenen Commits: 82 (der gesamte Firmware-Anteil).
Verifiziert: alle 7 Targets bauen mit `-Werror` sauber.

---

## Stability-Release v4.35p.08.27-stability (2026-08-27)

Sechs behobene Defekte, ein neues Diagnosewerkzeug und eine Testschicht, die
echte Feldmitschnitte durch den ausgelieferten Code nachfaehrt statt durch eine
Nachbildung.

`FLASH_VERSION` steht jetzt auf `20260827`. Das ist ab diesem Release
gefahrlos: die Ruecksetzbedingung haengt seit dem ersten Punkt unten an
`FLASH_STRUCT_VERSION` (unveraendert `20260724`), nicht mehr am Build-Datum.
Aktualisierende Knoten behalten Rufzeichen, WLAN-Zugangsdaten und
Sensorkonfiguration; auf beiden MCU-Familien nachgemessen.

### Was sich auf dem Draht aendert

Fast alles in diesem Release ist von aussen unsichtbar. Drei Punkte sind es
nicht, und sie stehen hier, damit sie niemanden ueberraschen:

- **`/B=000` wird jetzt gesendet**, wenn die Batterie gemessen und leer ist.
  Bisher ging unterhalb von einem Prozent gar nichts raus -- "leer" und "keine
  Batterie bestueckt" waren nicht unterscheidbar. Ein fehlendes `/B=`-Tag heisst
  ab jetzt: dieser Knoten hat keine Batteriehardware zu melden.
- **Unplausible Frames im ACK-Pfad werden nicht mehr weitergesendet.** Gegen
  8741 Feldframes gemessen sind das 5,7 % dessen, was diesen Pfad erreicht --
  und kein einziger davon quittierte eine Nachricht, die der Knoten tatsaechlich
  gehoert hatte. Bisher wurden sie mit Prioritaet 1 ins Mesh zurueckgesendet.
- **Die ESP32-Kanalauslastung faellt deutlich**, weil sie aus einer festen
  Laenge von 255 Byte gerechnet wurde. Am Funk hat sich nichts geaendert, die
  Zahl stimmt jetzt bloss. Wo derselbe Knoten `util=18%` meldete, sind real rund
  7 % zu erwarten.

### Settings-Persistenz -- jedes Release hat die Konfiguration geloescht (Critical)

Die Ruecksetzbedingung lautete `node_fversion != FLASH_VERSION`, und
`FLASH_VERSION` ist ein Datum, das pro Release hochgezogen wird. Damit hat
JEDES Release die gespeicherte Konfiguration jedes aktualisierenden Knotens
verworfen -- Rufzeichen, WLAN-Zugangsdaten, Sensor- und Netzeinstellungen --,
auch wenn sich am Aufbau von `struct s_meshcom_settings` nichts geaendert hatte.
Nachweisbar am Sprung `20260724` -> `20260821`: der Commit hat weder
`src/esp32/esp32_flash.h` noch `src/nrf52/WisBlock-API.h` angefasst, alle Knoten
wurden trotzdem zurueckgesetzt.

Build-Kennung und Layout-Generation sind jetzt getrennt. `FLASH_VERSION` bleibt
die Release-Kennung und ist rein informativ (`--info`); `FLASH_STRUCT_VERSION`
benennt die Generation des Settings-Layouts und ist der einzige Wert, ueber den
`clear_flash()` entscheidet. Sie steht auf `20260724`, der letzten echten
Layout-Aenderung. Knoten, die nach der alten Semantik `20260821` gespeichert
haben, geniessen Bestandsschutz und werden nicht zurueckgesetzt. Die Logzeile
benennt beides getrennt:

    [INIT]...FLASH layout 20260821 ok, build 20260827

`--flash-reset` setzt weiterhin zurueck.

### Batterie -- ADC_CTRL-Polaritaet wird gemessen statt geraten

Der Umschalter fuer den Spannungsteiler wurde per Compile-Zeit-Test gewaehlt:

    #if defined(BOARD_HELTEC_V31) || defined(BOARD_WIRELESS_PAPER)

`BOARD_HELTEC_V31` ist im gesamten Baum NIRGENDS definiert -- weder in einer
`variants/*/configuration.h` noch in `platformio.ini` noch in `build_flags`. Der
active-LOW-Zweig war damit in jedem je gebauten Image unerreichbar, und jeder
Heltec V3 wurde active-HIGH angesteuert, unabhaengig davon, ob die Platine real
eine 3.0 oder eine 3.1 ist.

Die Polaritaet wird jetzt einmalig beim Boot gemessen. Die Signatur ist
eindeutig und am Geraet nachgemessen: ein durchgeschalteter Teiler liefert
dreistellige Rohwerte, ein getrennter Pin einstellige (enable=HIGH 902-906
Counts, enable=LOW 1-4 Counts, Schwelle 50). Auf einem Board mit bereits
korrekter Polaritaet ist die Aenderung ein No-op -- auf einem Heltec V3 mit
realem Akku bestaetigt. Der Compile-Zeit-Wert bleibt als Startwert und
Rueckfallebene erhalten.

Daraus faellt zum ersten Mal die Unterscheidung "kein Teiler bestueckt" gegen
"Akku leer" ab, auf der der naechste Punkt aufbaut.

### Batterie -- `/B=` auch bei 0 Prozent senden

`mv_to_percent()` klemmt unterhalb `BAT_MIN_VOLTAGE` auf 0, und das `/B=`-Tag
wurde nur `if(global_proz > 0)` geschrieben. Ein fast leerer Akku meldete damit
gar nichts -- der Batteriegraph wird genau dann leer, wenn man ihn braucht. Aus
einer Netzmessung ueber 1230 Stationen: eine reale Station meldet Batterie in
einem Sechs-Stunden-Fenster pro Tag und sonst nie, nur weil der Pack die
3,3-V-Linie kreuzt.

Die beiden `/B=`-Erzeuger in `PositionToAPRS()` waren sich zudem uneinig: der
INA226-Zweig schrieb `/B=%i` voellig ungeprueft, der normale Zweig `/B=%03d` nur
oberhalb 0. Beide schreiben das Tag jetzt, sobald Batteriehardware erkannt ist.
Ein gesendetes `/B=000` heisst "gemessen, und der Akku ist leer"; ein fehlendes
Tag heisst "dieser Knoten hat keine Batterie zu melden". Rueckwaertskompatibel:
der Empfaenger liest das Feld mit `sscanf("%d")`, alle drei Schreibweisen
ergeben denselben Integer.

Mitbehoben: der PMU-Ausfallpfad in `esp32_main.cpp` setzte beide Werte auf 0,
ohne sie als "nicht messbar" zu kennzeichnen. Ohne diesen Zusatz haette ein
T-Beam mit defekter PMU ab sofort dauerhaft `/B=000` gesendet und damit "Akku
leer" behauptet, wo "keine Messung moeglich" gilt.

### ACK-Pfad -- Textbruchstuecke wurden als ACK ins Mesh geflutet (High)

`handleACK()` prueft bisher nur `payload[0] == 0x41` und eine Mindestlaenge.
0x41 ist als ASCII der Buchstabe `A` -- jedes Bruchstueck eines Text- oder
Positionspakets, das damit beginnt, lief dadurch durch die ACK-Verarbeitung:
Bytes 1..4 galten als seine `msg_id`, es landete im Dedup-Ring, wurde mit
Prioritaet 1 in die Sendequeue geschrieben, warf dabei einen Heartbeat aus der
vollen Queue und wurde schliesslich ins Mesh weitergesendet.

Byte 5 ist der belastbare Diskriminator: Funk-ACKs entstehen ausschliesslich als
`(0x80 | max_hop)` und werden auf dem Weiterleitungspfad nur dekrementiert.
Feldmessung ueber 32,7 h und 8741 Frames im ACK-Pfad, drei Knoten:

| Byte 5            | Anzahl        | Byte 10/11 == 01 00 | Feld 6..9 real gehoert |
| ----------------- | ------------- | ------------------- | ---------------------- |
| 0x80..0x84        | 8235 (94,2 %) | 99,6 %              | 58,9 %                 |
| 0x80\|Hops 5..116 | 221 (2,5 %)   | 0,0 %               | 0,0 %                  |
| Bit 7 = 0         | 285 (3,3 %)   | 0,0 %               | 0,0 %                  |

Kein einziger der 506 unplausiblen Frames quittierte eine Nachricht, die der
Knoten tatsaechlich gehoert hatte. Besonders relevant ist die mittlere Gruppe:
eine reine Server-Bit-Pruefung haette sie durchgelassen, und ausgerechnet diese
Frames fuehren Hop-Budgets bis 116. Die Pruefung sitzt als reine Funktion in
`ack_functions.h`, damit sie ohne Hardware testbar ist; ein verworfener Frame
wird als `ACK_REJECT` protokolliert statt still fallengelassen.

### {SET} -- max_hop ohne jede Bereichspruefung

`sscanf` schrieb die Werte direkt in `meshcom_settings`. Ein Tippfehler wie
`{SET}44;2;` landete damit ungeprueft im Hop-Feld jedes ausgesendeten Pakets --
und der Weiterleitungspfad prueft nur `(byte5 & 0x7F) > 0` und dekrementiert,
nach oben war nichts begrenzt. Werte ausserhalb `0..MAX_HOP_LIMIT` lassen den
bisherigen Wert jetzt stehen. Die bisherige Nachsichtigkeit bleibt: jedes Feld
wird einzeln uebernommen, sobald `sscanf` es gelesen hat.

### Neu -- `--txcapture`, Rohframe-Mitschnitt zur Laufzeit

Bisher zeigte das Log Frames nur DEKODIERT, also das Ergebnis unseres eigenen
Parsers. Ein Frame, den der Decoder falsch liest, steht falsch geparst im Log;
nichts darin verraet, was auf dem Kanal lag.

`captureFrame()` legt Rohframes jetzt in einen byteorientierten Ring (768 B),
`captureDrain()` gibt sie aus dem Loop aus. Empfang haengt an `--loradebug`,
Senden am neuen Schalter `--txcapture on/off` (persistiert). Der Ring ist kein
Beiwerk: direkt im Radio-Callback braucht `printfdeb()` rund 900 B Stack -- der
nRF52-Timer-Task hat 1 KB -- und rund 48 ms Serial-Zeit im RX-Pfad. Auf der
TX-Seite saesse er zwischen CAD-Entscheidung "Kanal frei" und `startTransmit()`
und wuerde die Kanalmessung entwerten, auf der der Sendezeitpunkt beruht.
Entkoppelt kostet die Erfassung ein `memcpy`.

Verworfene Frames meldet `[MC-TEST] CAPTURE_DROPPED n= serial_bytes=` -- der
Mitschnitt wird genau dann lueckenhaft, wenn der Kanal voll ist, also in den
Kollisionslagen, um derentwillen man ihn einschaltet. Kosten auf RAK4631:
+1400 B RAM, +1616 B Flash.

### N-29 -- `checkRX()` meldete fuer JEDEN Empfang 255 Byte (High)

Vom neuen Mitschnitt bei seinem allerersten Lauf auf echter Hardware gefunden.

    size_t ibytes = UDP_TX_BUF_SIZE;          // 255
    state = radio.readData(payload, ibytes);

RadioLib nimmt `len` per WERT und schreibt die tatsaechliche Laenge nicht
zurueck; sie ist nur eine obere Schranke. `SX126x.h` sagt das ausdruecklich:
"getPacketLength method must be called BEFORE calling readData!". `ibytes` blieb
also immer 255, und hinter dem echten Frame lag der uninitialisierte
Stackinhalt.

Folge 1, Kanalauslastung um Faktor 1,8-4,1 zu hoch: `checkRX()` bucht
`getTimeOnAir(ibytes)`. Nach jedem Empfang stand exakt `rx=2476ms` im Log -- die
Sendedauer eines 255-Byte-Pakets bei SF11/BW250/CR4:6. Die echten Frames lagen
bei 608 ms (48 B) bis 1394 ms (133 B). Die Gegenprobe steht in derselben Zeile:
`tx=701ms` passt aufs Byte zum 60-Byte-TX-Frame, denn der Sendepfad kennt seine
Laenge. Aus `util=18%` wurden real rund 7 %. Alle ESP32-Auslastungszahlen dieser
Firmware sind zu hoch und waren nie mit nRF52-Zahlen vergleichbar.

Folge 2, Diagnose und Korpus: `CRC_PAYLOAD`, `ERR_PAYLOAD` und der Mitschnitt
hingen rund 190 Byte RAM-Inhalt an jeden Frame.

Die Laenge wird jetzt vor `readData()` mit `radio.getPacketLength()` vom Chip
gelesen, auf `UDP_TX_BUF_SIZE` gedeckelt, und `ibytes == 0` gilt nicht mehr als
Frame.

### N-30 -- `printfdeb()` verdoppelte jedes `%%`

Der `%%`-Zweig schrieb zwei Prozentzeichen und fiel dann in die allgemeine
Kopie, die dasselbe Zeichen erneut anhaengte. Sichtbar als `util=18%%` und
`BATT 100 %%`. Trifft jede Logzeile mit Prozentzeichen und damit auch
`loganalyse.sh` und `logharvest.py`. Die Umbaulogik liegt jetzt in
`src/printfdeb_format.h`, damit sie ohne Arduino nativ pruefbar ist. Mitgefixt:
bei fuehrendem `;` las die alte Schleife vor den Puffer.

### N-31 -- `--info` gab Passwoerter im Klartext an die offene Netzkonsole

`node_passwd`, `node_webpwd` und `node_pwd` wurden unmaskiert gedruckt. Diese
Ausgabe laeuft ueber `printfdeb()` und damit auch auf Port 2323 -- und die
Konsole verlangt ohne gesetztes `node_passwd` keine Authentisierung.
Nachgestellt: `nc <node> 2323`, dann `--info`, und das WLAN-PSK steht auf dem
Schirm. Dieselbe Ausgabe steht in jedem geteilten Logmitschnitt.

`maskSecret()` ersetzt ein gesetztes Passwort durch `***`; leer bleibt leer,
damit ablesbar bleibt OB eins gesetzt ist. Die Settings-JSON an die App bleibt
unberuehrt, sie muss den Wert zum Anzeigen und Aendern tragen. Das ersetzt nicht
`--passwd`, es nimmt dem offenen Port den lohnendsten Fund.

### Tests -- Replay gegen ueber 90 Knotenstunden Feldmitschnitt

`tools/traceharvest.py` erntet die Entscheidungsfolge laufender Knoten aus der
`[MC-DBG]`-Ausgabe und fuettert sie dem echten Code, keiner Nachbildung:

| Suite                | Gegenstand                             | Umfang                             | Abweichungen |
| -------------------- | -------------------------------------- | ---------------------------------- | ------------ |
| `test_dedup_replay`  | `is_new_packet()`, `addLoraRxBuffer()` | 8813 Urteile, 10652 Slotbelegungen | 0            |
| `test_txprio_replay` | `getMessagePriority()`                 | 729 Einstufungen, 5 Klassen        | 0            |
| `test_ack_replay`    | `isPlausibleAckFrame()`                | 30 im Feld honorierte ACKs         | 0            |

Der ACK-Filter ist juenger als diese Logs, es gibt also kein geloggtes Urteil
zum Abgleich. Stattdessen wird eine WIRKUNG genutzt: auf jeden dieser Frames
folgte ein `ACK_RECEIVED`, das einen wartenden Ringslot geschlossen hat. Damit
ist die Frage beantwortbar, die ein nachtraeglich eingezogener Filter immer
aufwirft -- schneidet er ins Fleisch? Er tut es nicht. Alle drei Suiten sind
mutationsgeprueft.

Dazu `test_aprs_reencode` als Interop-Orakel: `encodeAPRS()` reproduziert die
Bytesumme von 2650 verschiedenen real gehoerten Frames AUSNAHMSLOS, ueber
neunzehn gemeldete Hardware-IDs und zwei Firmware-Generationen. Kein
Zirkelschluss -- die Pruefsumme stammt vom Absender, der Decoder verwirft den
Frame, wenn sie nicht zu den Wire-Bytes passt. `test_aprs_fuzz` schickt 500 real
beschaedigte Frames unter ASan und UBSan durch den Parser, mit
Fuell-Differential gegen Lesen ueber das Frame-Ende hinaus.

### Dedup-Ring bleibt bei 100 -- gemessen, nicht geschaetzt

Ein Auswertungsbericht empfahl 100 -> 300. Gegenprobe an denselben 48
Knotenstunden: 112 Rueckkehrer, davon genau **ein** echtes Duplikat, 96
`msg_id`-Wiederverwendungen (eine ANDERE Nachricht mit gleicher ID, Haeufung bei
180-210 min Abstand) und 15 nicht aufloesbare Faelle.

| Ring        | zusaetzlich abgefangen | faelschlich verworfen | Bilanz |
| ----------- | ---------------------- | --------------------- | ------ |
| 150/200/300 | 1                      | 1                     | +-0    |
| 500         | 1                      | 52                    | -51    |
| 1000        | 1                      | 96                    | -95    |

Die Diagnose des Berichts stimmt: das Fenster liegt bei rund 38 min gegen eine
laengste beobachtete Paketlebensdauer von 36,7 min, also ohne Reserve. Nur
faellt durch diese Luecke praktisch nichts. 1 kB RAM fuer ein Ereignis in 48
Knotenstunden ist kein Geschaeft. Die Messung steht als Begruendung im Code, und
`test_dedup_replay` laesst eine Aenderung der Zahl absichtlich auffliegen.

### Werkzeuge

- `tools/meshlogger.py` schneidet die Netzkonsole eines Knotens ueber Tage auf
  Platte mit. Die Konsole bedient einen Client zur Zeit, deshalb gibt das Tool
  den Port auf eine `PAUSE`-Datei hin frei.
- `tools/loganalyse.sh` wurde selbst geprueft und korrigiert (TOOL-01 bis
  TOOL-06): CSMA-Backoff wurde als State-Machine-Fehler gezaehlt, die
  Drop-Aufschluesselung landete im falschen Topf, die Hop-Extraktion las das
  falsche Feld, Stoerbytes in der Eingabe brachten es zum Straucheln, und
  Rohlogs mussten von Hand vorbereitet werden. Es liest sie jetzt direkt, und
  eine Regressionssuite deckt die drei Zaehlfehler ab. Ein Urteil aus einem
  kaputten Messgeraet ist nichts wert -- mehrere Aussagen dieses Release ruhen
  auf diesem Werkzeug.

### Was fuer dieses Release auf Hardware geprueft wurde

Die Nachweise stammen jeweils aus dem Lauf zu der Aenderung, die sie belegen --
nicht aus einem einzelnen Abschlusslauf des fertigen Baums. Fuer den
Release-Stand selbst stehen der Vollbau aller Environments und die native
Testsuite.

- **Heltec V3** -- OTA-Update von `20260821`, Settings unveraendert
  (`FLASH layout 20260821 ok, build 20260827`), zweite OTA im eingeschwungenen
  Zustand ebenfalls ohne Loeschung. ADC_CTRL-Probe `high=971 low=0 -> active
HIGH`, BATT 4.14 V / 90 %, Roh-ADC-Streuung +/-1, Positions-Beacon ausgeloest
  und gesendet (TX_START/TX_DONE), kein `task_wdt`-Ereignis. Rohframe-Mitschnitt
  ueber die Netzkonsole auf Port 2323, fuenf Minuten -- der Lauf, der N-29
  aufgedeckt hat.
- **T-Beam v1.2** (ESP32-D0WDQ6, AXP2101, SX1276) -- OTA-Update, Rufzeichen und
  WLAN-Zugangsdaten erhalten. AXP2101-Batteriepfad unveraendert, BATT 4.15 V /
  100 %, kein `task_wdt`. Der PMU-Ausfallzweig greift dort erwartungsgemaess
  nicht.
- **WisBlock RAK4631** (nRF52) -- DFU-Update, Rufzeichen und
  Ethernet-Konfiguration erhalten, Flash-Version danach `20260724`.

Dazu 6 native Environments mit 220 Testfaellen und der Vollbau aller
Release-Environments.

### Was ausdruecklich NICHT geprueft wurde

- **Der Batterie-Nullpunkt am realen Akku.** `/B=000` ist aus der Logik und aus
  Tests belegt, aber kein Pack auf der Bank war leer genug, um die Meldung im
  Feld auszuloesen.
- **Der INA226-Zweig** von `PositionToAPRS()`. Auf der Bank stand kein Board mit
  INA226; die Aenderung dort ist eine Formatangleichung von `%i` auf `%03d`.
- **Die `--txcapture`-Sendeseite ueber ein echtes Radio.** Der Empfangspfad hat
  auf dem Heltec V3 Frames geliefert, der TX-Mitschnitt ist im selben Lauf mit
  drei Frames aufgetreten, aber nicht systematisch gegen einen Zweitempfaenger
  gehalten.
- **Boards ausserhalb der drei genannten.** Sie bauen aus derselben Quelle und
  erben jede Verbesserung, standen aber nicht auf der Bank -- inklusive T-Beam
  Supreme und aller nRF52-Boards ausser dem RAK4631.
- **Batteriestart ohne USB-Host**, unveraendert seit dem letzten Release nicht
  durchfuehrbar.

---

## Stability-Release v4.35p.08.22-stability (2026-08-22)

> **Nachtrag:** Das Binary dieses Release vergleicht noch das Build-Datum und
> loescht daher beim naechsten Datumssprung die Einstellungen. Die Trennung von
> `FLASH_VERSION` (Build-Kennung) und `FLASH_STRUCT_VERSION` (Layout-Generation)
> ist mit `v4.35p.08.27-stability` veroeffentlicht; ein Update von hier dorthin
> behaelt die Konfiguration.

Korrektur-Release auf `v4.35p.08.21-stability`. **Wer 08.21 installiert hat,
sollte aktualisieren**: dieser Stand hatte einen Defekt, der jeden Node mit
aktiviertem GPS in einen dauerhaften Boot-Loop schickte. Das Release 08.21
wurde deshalb geloescht.

`FLASH_VERSION` bleibt bewusst auf `20260821`. Ein Bump wuerde in
`esp32_main.cpp:732` die gespeicherten Einstellungen jedes aktualisierenden
Knotens loeschen -- Rufzeichen, WLAN-Zugangsdaten, Sensorkonfiguration. Die
Settings-Struktur hat sich nicht geaendert, also gibt es keinen Grund dafuer.

### N-25 -- GPS-Baudscan loest den Task-Watchdog aus (Critical)

Unser eigener Regress, eingefuehrt mit `4c21cb49` (Audit-Befund C3): der
Task-Watchdog wurde in der ersten Zeile von `esp32setup()` abonniert.
`WZ_GPS_Init()` laeuft aber aus `esp32loop()` und blockiert dort rund 16 s ohne
eine einzige Fuetterung -- acht Baudraten zu je 1500 ms, dazu `GPSprobe()` mit
unbegrenztem `readUBX()`-Schwanz. Bei einem Timeout von 5 s bricht der Knoten
zwei bis drei Baudstufen nach Scan-Beginn ab. Da `--gps on` vor dem Absturz
persistiert wird, wiederholt sich das bei jedem Boot: kein Kommandofenster,
keine Rettung ueber Funk, BLE oder Netzkonsole -- nur Reflash ueber USB.

Vier Aenderungen:

- **S1** -- die Watchdog-Subskription wandert ans Ende von `esp32setup()`.
  Setup fuehrt legitim sekundenlange Einmal-Initialisierung aus; sie zu
  ueberwachen war nie Sinn von C3 und hat elf diagnostizierbare Haenger in
  anonyme Boot-Loops verwandelt.
- **S4** -- der GPS-Init-Pfad fuettert den Watchdog, hinter dem einzigen
  Helfer `src/watchdog_feed.h`, der ausserhalb von ESP32 zu nichts kompiliert.
- **S5** -- der Scan bricht bei der ersten NMEA-Sequenz mit gueltiger
  Pruefsumme ab, statt immer alle acht Baudraten zu durchlaufen und danach per
  Argmax die mit den meisten Zeichen zu waehlen. Der alte Argmax hatte keine
  Mindestanzahl (Befund B-15) und konnte aus einem einzelnen Rauschbyte eine
  Phantom-Baudrate erfinden.
- Die Baudratentabelle ist nach Trefferwahrscheinlichkeit sortiert. `SetupUBLOX()`
  stellt Module beim ersten Init dauerhaft auf 38400; mit 9600 vorn lief das
  9600-Fenster danach bei jedem Boot voll aus.

Gemessen, Heltec V3 mit u-blox: **Erkennung von 12 000 ms auf 321 ms**, auf
einem T-Beam bis herunter auf 24 ms.

Zusaetzlich die Aufraeumwelle im selben File: die tote ISR-Variante der
Baudratenerkennung ist entfallen (A-1 bis A-4), inklusive zweier `return -1`
aus einer `unsigned long`-Funktion und unseres eigenen
`pulseIndex + 1`-Regresses. Dabei kam heraus, dass `gps_functions.cpp:24` die
Variantenoption `GPS_BAUDRATE_SOFTCHECK` aus 15 `configuration.h` unbedingt
ueberschrieben hat -- der zweite Zweig war auf ALLEN Boards unerreichbar. Die
Flash-Groesse ist vorher und nachher byteidentisch, was den toten Code belegt.

Volle Analyse: [`docs/archive/bug-N25-gps-baud-scan-watchdog.md`](docs/archive/bug-N25-gps-baud-scan-watchdog.md).
Messprotokoll der Bankpruefung: [`docs/archive/gps-sensor-bench-20260822.md`](docs/archive/gps-sensor-bench-20260822.md).

### N-27 -- BME680-Treiber haelt ein I2C-ACK fuer eine Chip-Erkennung (Medium)

`setupBME680()` setzte `bme680_found` aus dem blossen Adress-ACK und verwarf
den Rueckgabewert von `bme.begin()` -- dabei ist genau das die Stelle, die die
Chip-ID liest. 0x76 und 0x77 teilen sich BME280, BMP280 und BME680. Auf einem
Board mit BME280 meldete der Node `BME680: on (found)` und druckte in jedem
Lesezyklus `Failed to complete reading :(`.

### N-28 -- `--bmx off` konnte den BME680 nicht abschalten (Low, UX)

Die Hilfe sagt seit jeher `--bmx BME/BMP/680 off`, der Handler loeschte aber
nur BMP280/BME280/BMP390. Wer der Hilfe folgte und danach `--bme on` gab, bekam
`BME680 and BMx280 can't be used together!` und stand ohne Sensor da.

### Build -- Upload-Port festgenagelt

Die 27 `upload_command`-Zeilen in `variants/*/platformio.ini` trugen kein
`--port`, `esptool` suchte sich den Port also selbst. Mit zwei angeschlossenen
Boards flasht `pio run --target upload` dann potenziell das falsche. `--upload-port`
war wirkungslos, weil PlatformIO es nur ueber `$UPLOAD_PORT` weiterreicht.

### Was fuer dieses Release auf Hardware geprueft wurde

- **Heltec V3** mit u-blox-GPS und BME280 -- Boot-Loop auf dem alten Stand
  reproduziert und hier als behoben bestaetigt; GPS-Neuinitialisierung per
  Kommando und per Dreifachklick; 36 Minuten Dauerlauf mit GPS und Sensor;
  Sensortyp korrekt bestimmt.
- **Heltec V3, zweites Geraet, ohne GPS-Modul** -- Scan laeuft zu Ende, meldet
  ehrlich einen Fehlschlag und erfindet keine Baudrate.
- **T-Beam v1.2** (ESP32-D0WDQ6, AXP2101, SX1276, u-blox) -- neu in Betrieb
  genommen. Boot mit WLAN-Verbindung und GPS-Init im selben Durchlauf, also
  genau die Sequenz, an der es vorher scheiterte.
- **WisBlock RAK4631** (nRF52) -- geflasht und betrieben. Auf nRF52 ist
  ueberhaupt kein Task-Watchdog scharf, der Defekt trifft diesen Pfad nicht.

### Was ausdruecklich NICHT geprueft wurde

- **T-Beam Supreme.** `ttgo_tbeam_supreme.bin` ist wieder im Release -- der
  Grund fuer das Zurueckhalten war dieser GPS-Defekt, und der ist behoben. Auf
  einem Supreme verifiziert wurde es aber nicht. Konkrete Luecke: der Supreme
  traegt ein **L76K**, beide Module auf der Bank waren u-blox. Der
  L76K-Zweig in `GPSprobe()` ist von keinem Test beruehrt.
- **Batteriestart ohne USB-Host.** Auf dieser Bank nicht durchfuehrbar: dem
  Heltec V3 ist `ARDUINO_USB_CDC_ON_BOOT=1` auskommentiert, der klassische
  T-Beam hat kein natives USB.
- **Der Phantom-Baudraten-Fall mit verrauschtem Eingang.** Belegt ist, dass
  keine Phantom-Baudrate gemeldet wird; der freie Eingang las null Zeichen, der
  eigentliche Ausloeser liess sich also nicht erzeugen.

---

## Stability-Release v4.35p.08.21-stability (2026-08-21)

> **Zurueckgezogen.** Dieses Release hatte den oben beschriebenen Defekt N-25
> und wurde geloescht. Nicht mehr installieren.

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
und [`docs/archive/code-audit-fixes-20260627.md`](docs/archive/code-audit-fixes-20260627.md)
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

- `t5_epaper`, `vision-master-e213-preview`, `esp32-external-radio` -- Opt-in,
  nicht in `default_envs`.

`T-ETH-ELITE_1262` steht zwar nicht in der Artefaktliste des Release-Workflows,
wird aber seit 08.21 von Hand mitveroeffentlicht und ist im Release enthalten.

### Newer version > v4.35 able to upgrade via OTA-Flasher.

### [MeshCom Changelog](https://icssw.org/meshcom-versionen/)

### [MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
