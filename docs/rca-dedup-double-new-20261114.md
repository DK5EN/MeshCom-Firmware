# RCA: dieselbe Message-ID mehrfach als RX_DEDUP_NEW (Soak DK5EN-98, 2026-11-08/09)

**Eigene Baken und HEY kommen beim Senden nicht in den Dedup-Ring, darum ist jedes Relay-Echo wieder „neu“. Kein Funktionsfehler, aber eine undokumentierte Inkonsistenz, die DUP/NEW verfälscht.**

## Kurzbefund

- **Zahlenkorrektur:** Es sind 112 Message-IDs mit mindestens zwei NEW, nicht 160. Die 160 sind die zusätzlichen NEW-Ereignisse (112 IDs: 68 mit 2, 40 mit 3, 4 mit 4 NEW). (`dedup_double_new.py`, `dedup_own_types.py`)
- **Eigen oder fremd:** 111 der 112 IDs sind eigene Frames (checkOwnTx trifft direkt vor dem NEW), 1 ist fremd. Von den 160 Folge-NEW sind 159 eigene, 1 fremd.
- **Abstand unter 60 s:** 110 von 160 Paaren, alle 110 eigen, kleinster Abstand 2,15 s. Das fremde Paar liegt bei 60426 s.
- **Frame-Typen:** Alle 301 NEW-Ereignisse eigener IDs sind Echos von HEY (197, `@`) und Position (104, `!`). Text-Echos (`:`) sind 140 von 140 DUP. (`dedup_own_tx_types.py`)
- **Eigene Sendungen im Zeitraum:** 102 HEY, 46 Positionen, 46 Texte. Der Ring bekam genau 46 RX_DEDUP_ADD für diese IDs, alle für Texte, keinen für HEY oder Position.

## Mechanismus

- **Was verglichen wird:** `is_new_packet()` (`src/dedup_functions.cpp:23`) vergleicht nur 4 Byte, die msg_id, per `memcmp` (`src/dedup_functions.cpp:27`). Pfad, Hop, Flags und FCS fließen nicht ein.
- **Aufruf je Frame:** `OnRxDone()` (`src/lora_functions.cpp:870`) ruft `is_new_packet(RcvBuffer+1)` genau einmal (`src/lora_functions.cpp:1213`). Das Byte 0 ist der Typ, Byte 1 bis 4 die ID.
- **Nur Suche:** `is_new_packet()` schreibt nichts. Ein Eintrag entsteht nur durch `addLoraRxBuffer()` (`src/dedup_functions.cpp:57`).
- **Eigene Echos laufen vorbei:** Trifft `checkOwnTx()` (`src/loop_functions.cpp:1067`, Aufruf `src/lora_functions.cpp:1209`), nimmt OnRxDone den Zweig `if(heardIcheck >= 0)` (`src/lora_functions.cpp:1730`).
- **Eintrag nur im Fremdzweig:** Der RX-seitige `addLoraRxBuffer()` (`src/lora_functions.cpp:1861`) liegt im `else if(setlogCountDedup(rx_is_new))` (`src/lora_functions.cpp:1793`). Ein eigenes Echo erreicht ihn nie.
- **Eintrag beim Senden:** Eigene IDs kommen nur dort in den Ring, wo die Sendefunktion es selbst tut: `sendMessage()` (`src/loop_functions.cpp:5192` bis 5198), `sendInjectedPosition()` (`src/loop_functions.cpp:5292` bis 5296) und `SendAckMessage()` (`src/loop_functions.cpp:6042` bis 6047).
- **Kein Eintrag bei Bake und HEY:** `sendPosition()` (`src/loop_functions.cpp:5911`), `sendAPPPosition()` (`src/loop_functions.cpp:5989`) und `sendHey()` (`src/loop_functions.cpp:6155`) tragen nur in `own_msg_id[]` ein, nicht in den Dedup-Ring.
- **Kein Eintrag bei Bericht, Telemetrie, Ping, Pong:** `sendNbrReport()` (`src/loop_functions.cpp:6251`), `sendTelemetry()` (`src/loop_functions.cpp:6622`), `sendPing()` (`src/loop_functions.cpp:4270`) und `SendPong()` (`src/loop_functions.cpp:4366`) ebenso.
- **Beleg am Beispiel:** Für 1AE1E0BF steht `NEW-POS`/`TX-LoRa` um 18:58:54, dann zwei Echos (Pfad `DK5EN-98,DL3CSM-1` FCS 1D39, Pfad `DK5EN-98,DL2UD-1` FCS 1CEE) als RX_DEDUP_NEW mit `OWN:e`, ohne RX_DEDUP_ADD und ohne RLY. (all.log Zeile 253353 bis 253430)
- **Soak-weit:** Für die 111 eigenen Mehrfach-IDs gibt es 0 RX_DEDUP_ADD, 0 `RX-LoRa2`, 0 `RLY` und 0 relayte `TX`. (`dedup_delivery.py`)

## Folgen

- **Eigene Echos:** Der NEW/DUP-Ausgang ändert im Eigenzweig nur das `DUP:`-Feld der `[LOG]`-Zeile (`src/lora_functions.cpp:1236`). Relay, Speicherung und Telefon-Ausgabe liegen im Fremdzweig und werden nicht erreicht.
- **Neben-Effekt Zähler:** Im Eigenzweig läuft `setlogCountDedup()` nicht (nur PN-Sonderfall `src/lora_functions.cpp:1726`). Die Zeilen RX_DEDUP_NEW und der Zähler stat_newid driften damit auseinander.
- **HEY-Upload je Echo:** Bei den 74 mehrfach gehörten eigenen HEY-IDs gibt es 171 NEW, 171 GWU und 171 TX-UDP. Das ist gewollt: `heyEchoShouldUpload()` (`src/hey_echo_upload.h:30`) kennt kein Dedup-Verdikt. (`docs/hey-echo-upload-concept.md`)
- **Positions-Echos:** Bei den 37 mehrfach gehörten Positions-IDs gibt es 99 NEW, 0 GWU und 37 TX-UDP (je ID die eigene Erstsendung). Die Echos erzeugen keinen Upload.
- **Nachbarmatrix:** Die [NBR]-Zeilen (EDGE, ME, POS) erscheinen bei beiden Echos des Beispiels. Die NBR-Pflege hängt nicht am Dedup-Verdikt: `rx_is_new` und `rx_dup` werden nur an den Stellen `src/lora_functions.cpp:1213`, 1217, 1227, 1236, 1516, 1528, 1726, 1793 und 2717 gelesen.
- **Statistik:** 301 von 3142 NEW (9,6 %) sind eigene Echos. DUP/NEW liegt roh bei 1,24 (3895/3142), ohne eigene Echos bei 1,37 (3895/2841). Die Kennzahl M1 (`docs/adr-nc-importance-backoff.md:1685`) ist damit verzerrt.

## Fremde Frames

- **Fremde Mehrfach-NEW:** Genau eine ID, E686438A: 22:51:24 eine Position von DK1TCP-77 (Zeile 28214), 15:38:31 am Folgetag ein Text von DK1TCP-77 an DB0AU-12 (Zeile 215507).
- **Kein Duplikat:** Das sind zwei verschiedene Nachrichten mit derselben ID (Wiederverwendung des 10-Bit-Zählers, siehe `src/loop_functions.cpp:5870`). Dazwischen liegen 2045 Ring-Einträge bei 100 Plätzen, die ID war längst verdrängt.
- **Zustellung der drei Fremdbeispiele:** E686438A erscheint je einmal pro Nachricht mit ADD, GWU, RLY, TX-UDP und EXT-Out (also je Frame einmal, 2 Frames). Die Kontrollen 91A4352E und B02B829C haben je 1 NEW, 1 ADD, 1 GWU, 1 RLY, 1 TX-UDP, 1 EXT-Out, 1 relayten TX.
- **Doppelte Zustellung wegen NEW:** Keine gefunden. Unter den 2839 fremden IDs mit genau einem NEW haben 3 mehr als einen TX-UDP oder ADD (C05313A2, C05313A5, E9F113B3). Das sind spätere Wiederholungen (z. B. C05313A2 TX-UDP erneut um 14:46:32), keine zweiten NEW.

## Plattformen

- **ESP32-S3 (Messung):** DK5EN-98 ist ein Heltec V3 (Bootzeile, all.log Zeile 11), Ring 100 (`src/configuration_global.h:353`, im Log `slot=71/100`).
- **Gemeinsamer Code:** `is_new_packet()`, `addLoraRxBuffer()`, `OnRxDone()` und die Sendefunktionen stehen je einmal in `src/dedup_functions.cpp`, `src/lora_functions.cpp` und `src/loop_functions.cpp`.
- **nRF52 (Annahme):** Nach Code-Lesen gilt der Mechanismus auch für den RAK4630 (Ring ebenfalls 100). Gemessen ist er dort nicht.
- **Ringgröße:** Je nach Zweig 60, 100, 10 oder 70 Plätze (`src/configuration_global.h:339`, 353, 362, 379). Sie ändert nichts am Mechanismus, nur am Verdrängungsfenster für fremde IDs.

## Bewertung

- **Fehler oder gewollt:** Gewollt ist nur der Eigenzweig selbst (Echo wird nicht weitergeleitet, HEARD, HEY-Upload je Echo). Dass Bake, HEY, Ping, Pong, NBR-Bericht und Telemetrie nicht im Ring stehen, Text, ACK und Inject-Position aber schon, ist eine unbegründete Inkonsistenz.
- **Absicht nicht belegt:** In `docs/` und im Code finde ich keine Aussage, dass die ID eigener Baken bewusst fehlt (Suche nach `RX_DEDUP`, `own echo`, `Dedup` in `docs/*.md`). Die Annahme „absichtlich für Nachbarmatrix/Overhear-Cancel“ trägt nicht, weil die NBR-Pflege nicht am Verdikt hängt.
- **Schaden:** Kein Funktionsschaden im Soak. Verfälscht sind das `DUP:`-Feld und RX_DEDUP_NEW der Eigen-Echos, damit Kennzahlen wie M1 und die Lesart „NEW für gesehene ID heißt Ring umgelaufen“ (`docs/how-to-read-meshcom-logs.md`, Regel 6.14).

## Fixvorschlag (nicht umgesetzt)

- **Variante A, Firmware:** In den sieben Sendefunktionen (Bake, App-Position, HEY, NBR-Bericht, Telemetrie, Ping, Pong) neben `insertOwnTx()` den Block aus `sendMessage()` ergänzen (`addLoraRxBuffer(msg_id, bGATEWAY && node_hasIPaddress)`). Echos werden dann DUP.
- **Wirkung A:** Verhalten sonst unverändert, weil der Eigenzweig vor dem Dedup-Zweig liegt und `heyEchoShouldUpload()` das Verdikt nicht liest. Mehrbedarf rund 194 Ring-Einträge in 24 h (102 + 46 + 46), vernachlässigbar gegen 2885 ADD.
- **Variante B, nur Log:** In `OnRxDone()` das `DUP:`-Feld für Frames mit `icheck >= 0` als Duplikat ausweisen und die Zähler im Eigenzweig mitführen. Kein Ringeintrag, kleinster Eingriff.
- **Variante C, nur Doku:** Regel 6.14 in `docs/how-to-read-meshcom-logs.md` ergänzen: Mehrfach-NEW mit `OWN:e` sind Echos eigener Baken und HEY, kein Ringumlauf.
- **Empfehlung:** C jetzt, A oder B nur wenn M1 oder die Zählerbilanz diese Echos nicht ausfiltern können. A braucht einen Regressionstest (Echo einer Bake wird DUP, Relay und Upload unverändert).

## Skripte und Quellen

- **Skripte:** Alle in `/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/9c69045e-9026-43bf-b427-18b7064904aa/scratchpad/dedup/`: `dedup_double_new.py`, `dedup_own_types.py`, `dedup_delivery.py`, `dedup_own_tx_types.py`. Aufruf je `python3 -I <skript> <all.log>`.
- **Log:** `/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/9c69045e-9026-43bf-b427-18b7064904aa/scratchpad/soak/all.log` (268347 Zeilen, ein Boot bei Zeile 24, 3142 NEW, 3895 DUP, 2885 ADD).
- **Eigen-Erkennung:** Eine ID gilt als eigen, wenn `checkOwnTx:<ID> own_msg_id:` in den 6 Zeilen vor dem NEW steht. Die Zeile erscheint nur bei aktivem `bDisplayInfo` und Treffer.

## Was nicht geprüft ist

- **Keine Gegenprobe am Gerät:** Kein Build, kein Test, kein Zugriff auf Knoten. Der Fix ist nicht erprobt.
- **nRF52 nicht gemessen:** Die Aussage für RAK4630 beruht auf Code-Lesen, es liegt kein nRF52-Log vor. Plattform-Guards in den Sendefunktionen habe ich nur an `sendPosition()` stichprobenartig gesehen.
- **Absicht nicht geklärt:** Ob die Auslassung bei Bake und HEY bewusst war, lässt sich ohne Git-Historie und ohne Rückfrage an die Autoren nicht klären (Git war verboten).
- **Zählerbilanz offen:** Ob `DEDUP_BALANCE` (RX_DEDUP_NEW minus GWI gleich DUP:n) die 301 Eigen-Echos schon herausrechnet, habe ich nicht in `tools/` nachgelesen.
- **Wiederholte TX-UDP:** Die drei Fremd-IDs mit mehrfachem TX-UDP (C05313A2, C05313A5, E9F113B3) habe ich nicht auf die Ursache der Wiederholung (vermutlich UDP-Hold-Wiederholung, Annahme) untersucht.
- **Nur ein Soak:** 24 h auf einem Knoten mit 100er Ring; andere Ringgrößen und Verkehrslasten nicht gemessen. HEY-Zählung zählt nur Frames mit `TX x<ID> @ ... src=o`.
