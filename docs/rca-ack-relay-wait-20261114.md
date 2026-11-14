# RCA: Warum weitergeleitete ACKs der Prio 1 bis zu 25 s in der Sendeschlange warten (2026-11-14)

**Ein ACK der Prio 1 bekommt nur 3,0 bis 3,35 s Wartezeit, aber jeder empfangene Frame und jede Empfangs-Verschiebung startet diese Wartezeit neu. Das ist die dokumentierte CSMA-Regel, nicht ein Zählfehler; für ACKs greift erst die Alterungsstufe nach 20 s.**

Einschätzung: Mechanismus gewollt, Folge für ACKs nicht bewusst entschieden (Designlücke, kein Programmierfehler). Das Verhalten ist nur auf ESP32 gemessen, im Code gilt es auch für nRF52.

- Stand: 2026-11-14, `fork-dev`, Firmware `v4.40a.11.08`
- Umfang: Code lesen und das Soak-Log auswerten. Keine Quelldatei geändert, nichts gebaut, kein Knoten angesprochen.
- Log: `all.log` des Soak-Laufs (Zeilen im Text als `all.log:<Zeile>`), Zeitstempel Europe/Berlin

## Antworten auf die fünf Fragen

- **Frage 1, geplante Wartezeit:** Basis 3000 ms plus 0 bis 10 Slots zu je 35 ms, also 3000 bis 3350 ms beim ersten Versuch (`CSMA_PRIO_BASE_1`, `CSMA_PRIO_SLOTS_1`, `CSMA_SLOT_SIZE`, `src/configuration_global.h:513`, `:520`, `:427`).
- **Frage 2, erklärt das 6 bis 25 s:** Nein. Der Plan kennt nur 3,0 bis 3,35 s; die Wartezeit wird bei jedem Empfang neu gestartet (Abschnitt "Mechanismus").
- **Frage 3, zwei Slots für `0465F0B8`:** Das ist die gewollte Doppelkopie des Gateway-ACK (GWACK-01), kein Relay und kein Retry (Abschnitt "Das doppelte ACK").
- **Frage 4, nRF52:** Derselbe Code, derselbe Neustart der Wartezeit. Gemessen ist nur ESP32 (Abschnitt "Plattformen").
- **Frage 5, Schaden:** Der Absender sieht das ACK später, im Log bis 24,8 s. Sein Wiederholungsraster liegt bei etwa 40 s (Abschnitt "Schaden und Einschätzung").

## Zahlen aus dem Log

- **Menge:** 26 Sendungen mit `prio=1 src=r` und Typ `A`; alle weiteren Prio-1-Sendungen sind Typ `:` (Text), kein ACK.
- **Falsche Zuordnung in der Auswertung:** `src=r` umfasst Relay-ACKs und die ACKs, die das Gateway selbst erzeugt. 20 Sendungen sind echte Relay-ACKs, 6 sind Gateway-ACKs.
- **Relay-ACKs (`rx_ack_fwd`, n=20):** Median 6,06 s, p95 21,7 s, Maximum 24,8 s (`all.log:230452`, ID `578B35E1`).
- **Gateway-ACKs (`rx_dm_ack_gw`/`rx_dm_ack_new`, n=6):** Median 8,4 s, Maximum 21,9 s (`all.log:230403`, ID `0465F0B8`).
- **Über 10 s:** 9 der 26 ACK-Sendungen (die Soak-Auswertung zählt 9 von 53 für alle Prio-1-Sendungen, ACK und Text).
- **"Eigene ACKs, src=o, n=27":** Das sind eigene Textnachrichten (Typ `:`), keine ACKs (Beispiel ID `1AE1E018`, `all.log:7455`). Der Vergleichswert max. 6,2 s gehört zu Texten.
- **Häufung:** Die Wartezeiten liegen bei 3,06 bis 3,41 s, 5,9 bis 6,7 s, 9,7 bis 10,2 s, 13,1 bis 13,7 s, 15,9 s, 21,7 bis 21,9 s und 24,8 s. Das sind ganzzahlige Vielfache der 3-s-Wartezeit plus Zusatz.
- **Eine Wartezeit je Empfang:** Bei den Sendungen über 10 s lagen 2 bis 6 empfangene Frames im Wartefenster (Zählung von `RX_FLAG_PROCESS` zwischen Einreihung und Sendung).

## Mechanismus

- **Wartezeit wird bei jedem Empfang neu gesetzt:** `OnRxDone` ruft am Ende `iReceiveTimeOutTime = millis()` und `csma_timeout = csma_compute_timeout(cad_attempt)` (`src/lora_functions.cpp:2766`-`2767`).
- **Zweite Stelle auf ESP32:** Nach `checkRX()` setzt die Hauptschleife die Wartezeit noch einmal (`src/esp32/esp32_main.cpp:3960`). Beide Aufrufe ziehen den Zufall neu; das ist harmlos und erklärt je Empfang zwei `CSMA_AGED`-Zeilen (`all.log:230387`-`230388`).
- **Verschiebung bei laufendem Empfang:** Läuft die Wartezeit ab, während ein Preamble erkannt ist, wird nur der Anker neu gesetzt, die volle Wartezeit läuft erneut (`shouldDefer`, `src/esp32/esp32_main.cpp:3856`-`3860`; Log `RX_TIMEOUT_DEFERRED src=preamble_only`, `all.log:230361`).
- **Prüfung beim Ablauf:** Gesendet wird erst, wenn `millis() - iReceiveTimeOutTime >= csma_timeout` (`src/esp32/esp32_main.cpp:3790`-`3791`), danach folgt die Kanalprüfung CAD.
- **Dokumentiert:** Der Fixplan nennt genau das als "Problem 2": Ein empfangener Frame startet die volle Wartezeit neu, ein Frame im Kopf der Schlange wartet 60 s genauso wie ein frischer (`docs/campaign-lora-qrt-20261018.md:359`-`361`).
- **Zweck der 3 s:** Die Empfänger eines Frames halten 3,0 s, damit Nachbarn quittieren und weiterleiten können (`docs/meshcom-lora-qrt-fix-plan-20261018.md:317`).
- **Grund für die Dauer im Soak:** Der Kanal ist nicht leer. Die Frames kommen im Abstand von 3,4 bis 3,7 s (`all.log:230278`, `:230294`, `:230369`, `:230384`), also an der Grenze der Wartezeit, und setzen sie jedes Mal zurück.
- **Kanalstatus passt dazu:** `CAD_FREE attempt=0` bei allen drei Sendungen im Beispiel (`all.log:230306`, `:230400`, `:230449`), `CHANNEL_UTIL` 9 bis 19 % (`all.log:230322`, `:230390`). "Frei" gilt nur im Moment der Prüfung.
- **Prio 1 überholt die Wartezeit nicht:** Die Priorität wählt nur den Slot (`getNextTxSlot`, `src/txring_functions.cpp:232`) und die Basis (3000 statt 4500 ms). Sie verkürzt keinen laufenden Neustart.
- **Hop spielt keine Rolle:** `csmaTimeoutPrio` (`src/csma_timing.h:43`) hat keinen Hop-Parameter. Es zählen nur Priorität, Versuchszahl und Jitter.
- **ACK hat keinen NBR-Sonderweg:** Der abhängige Backoff gilt nur für Slots mit `RING_KIND_RELAY` (`src/lora_functions.cpp:4051`). ACK-Weiterleitungen sind `RING_KIND_OTHER` (`src/txring_functions.h:52`).

## Ausstieg: Alterungsstufe und CAD-Busy

- **Stufe 1 ab 20 s:** Ab Alter 20 s wird die Wartezeit nach einem Empfang auf `clamp(2850 - Sendedauer - 175, 500, 1500) + Jitter` begrenzt (`CSMA_AGE_STAGE1_MS`, `src/configuration_global.h:435`; `csmaAgedHoldMs`, `src/csma_timing.h:84`; `csmaApplyAgeFloor`, `:99`).
- **Gilt für Prio 1 bis 3:** Es gibt keine eigene, frühere Schwelle für ACKs (`src/csma_timing.h:102`; Plan: `docs/meshcom-lora-qrt-fix-plan-20261018.md:430`).
- **Beleg im Beispiel:** `CSMA_AGED slot=1 age=20247 hold=1675` (`all.log:230387`), danach ist die Wartezeit 1605 ms (`all.log:230392`) und `0465F0B8` geht nach 21,9 s hinaus (`all.log:230403`).
- **Zweiter Beleg:** `CSMA_AGED slot=3 age=21651 hold=1605` (`all.log:230433`), Sendung von `578B35E1` nach 24,8 s (`all.log:230452`).
- **Das Maximum 24,8 s ist keine Obergrenze:** Es entsteht aus 20 s Alterung plus einer Restwartezeit von 1,5 s plus einer Verschiebung. Im Lauf gab es 16 Neuberechnungen mit Alterungsstufe (32 `CSMA_AGED`-Zeilen, je Empfang zwei).
- **Nicht jede lange Wartezeit ist ein Neustart:** `04EC1298` wartete 5,9 s, weil CAD beim ersten Versuch "belegt" meldete und 2675 ms Backoff folgten (`all.log:249876`, `:249889`). Im Fenster lag kein empfangener Frame.
- **Gleiches Muster:** Auch `578805C9` (6,0 s) liegt im Fenster ohne empfangenen Frame, direkt vor einem `CAD_BUSY attempt=1` (`all.log:229351`). Die Ursache ist vermutet, nicht für jede Zeile einzeln geprüft.

## Das doppelte ACK (`0465F0B8`)

- **Zwei Schreibvorgänge im Ring:** `src=rx_dm_ack_gw` (Slot 0) und `src=rx_dm_ack_new` (Slot 1), 6 ms auseinander (`all.log:230241`, `:230245`).
- **Quelle:** Das Gateway hört einen Gruppentext (Ziel-Gruppe `26244`) und quittiert ihn selbst mit zwei Kopien derselben ID (`src/lora_functions.cpp:2342`, `:2353`).
- **Gewollt:** Die zweite Kopie ist eine Wiederholung für Hörer, die die erste verpasst haben (GWACK-01, `src/gw_ack_frame.h:49`, `docs/campaign-gwack01-20261024.md:12`).
- **Messgrundlage:** Pro Kopie verpassen Hörer etwa 25 % der Gateway-ACKs, mit zwei Kopien etwa 6 % (`docs/campaign-gwack01-20261024.md:16`).
- **Warum 12 s dazwischen:** Beide Kopien liegen im Ring. Nach der ersten Sendung (`all.log:230309`) startet `csma_reset()` die volle Wartezeit ohne Alterungsboden (`src/lora_functions.cpp:4121`) und die zweite wartet erneut.
- **Kein Retry und kein zweiter Empfang:** `rx_dm_ack_*` sind `RING_STATUS_DONE`, also ohne Wiederholungsleiter. Die Doppelung hat nichts mit der Empfangskopie `RX_DEDUP_DUP` bei `all.log:230383` zu tun.
- **Gleiche Zeile für andere IDs:** `0463496C` (3,2 s und 6,7 s) und `05012DF6` (3,4 s und 15,9 s) zeigen dasselbe (`all.log:229454`, `:229481`, `:266734`, `:266801`).

## Weitergeleitete ACKs werden nie durch gehörte Kopien gestrichen

- **Beobachtung:** `578B35E1` wurde in der Wartezeit zweimal von Nachbarn gehört (`all.log:230293`, `:230368`) und trotzdem gesendet (`all.log:230452`).
- **Ursache im Code:** Das Streichen durch gehörte Kopien (NBR-Cancel) trifft nur `RING_KIND_RELAY`-Slots (`src/lora_functions.cpp:1437`; Absprung für Nicht-Relay bei `:1375`).
- **Auch das Echo-Freigeben hilft nicht:** Es sucht Slots mit Status nicht `DONE` und nicht `READY`; ein weitergeleiteter ACK ist `DONE` (`src/lora_functions.cpp:667`, `:1092`).
- **Folge:** Ein ACK, das Nachbarn schon mehrfach weitergegeben haben, blockiert den Ring weiter und sendet am Ende noch einmal. Das ist Last, aber kein Fehler im Ablauf.

## Plattformen

- **ESP32:** Gemessen (DK5EN-98). Alle Zahlen oben gelten dafür.
- **nRF52, gleich:** `OnRxDone` ist plattformgemeinsam; die Zeilen 2766 bis 2767 liegen vor dem RAK4630-Block (`src/lora_functions.cpp:2766`-`2769`). Die Ablaufprüfung nutzt dasselbe `csma_timeout` (`src/nrf52/nrf52_main.cpp:1747`).
- **nRF52, anders:** Die Verschiebung bei laufendem Empfang prüft `is_receiving` mit bis zu 3 Verschiebungen (`src/nrf52/nrf52_main.cpp:1755`), ESP32 bei "nur Preamble" nur 1.
- **nRF52, Zusatz:** CAD läuft asynchron und braucht Schleifendurchläufe (CSMA-03); die Wartezeit kommt also eher dazu als ab. Der Umfang ist nicht gemessen.
- **Schluss:** Aus dem Code folgt dieselbe Wartezeit-Regel. Ob die Lücke dort ähnlich groß ist, hängt von der Verkehrsdichte ab und ist offen.

## Schaden und Einschätzung

- **Absender wiederholt erst spät:** Die Wiederholungsleiter eines eigenen Textes hat 40 s je Versuch (20 Ticks zu 2 s, `src/lora_functions.cpp:3369`-`3371`), höchstens 3 Wiederholungen.
- **Ein langer ACK-Hop ist noch kein Verlust:** 24,8 s bleibt unter 40 s. Mehrere Hops mit je 6 bis 25 s, dazu die Wartezeit des Textes selbst, können die 40 s überschreiten. Das ist eine Rechnung, nicht gemessen.
- **Wenn es überschritten wird:** Der Absender sendet den Text noch einmal und das Gateway quittiert erneut. Das erzeugt Last und kein Fehlverhalten beim Empfänger.
- **Sichtbar für den Nutzer:** Das Häkchen "quittiert" erscheint verzögert (Web-ACK-Ticks, `docs/ack-wer-hat-quittiert.md`). Eine Beschwerde dazu liegt nicht vor.
- **Bewertung:** Gewollt ist das Zurücksetzen der Wartezeit bei jedem Empfang. Nicht entschieden wurde, ob ein 12-Byte-ACK der höchsten Klasse dieselbe 20-s-Schwelle wie ein Text bekommen soll. Das Soak-Kriterium "über 10 s" war für Prio 1 berechtigt.
- **Betroffen ist nur eine kleine Menge:** 26 Prio-1-ACKs in 23,4 h bei 1659 Sendungen. Der Nutzen eines Eingriffs ist klein, das Risiko im Kanal nicht.

## Fixvorschlag (nur beschrieben, nicht umgesetzt)

- **A, Schwelle je Priorität (Empfehlung):** `csmaAgeStageEx` bekommt die Schwellen schon als Parameter (`src/csma_timing.h:71`). `csmaApplyAgeFloor` kann für Prio 1 eine frühere Stufe-1-Schwelle von 6 bis 8 s übergeben. Rein arithmetisch, im Test `test_csma_timing` prüfbar.
- **Wirkung von A:** Begrenzt das Maximum auf etwa 8 s plus Restwartezeit statt 25 s. Nach einer eigenen Sendung bleibt die volle Wartezeit (`csma_reset`), Nachbarn behalten ihr Fenster.
- **Risiko von A:** Mehr Sendungen innerhalb der 3-s-Fenster der Nachbarn, also mehr Kollisionen bei dichter Zelle. Braucht einen Bench-Lauf mit den Konstanten als Stellschraube, wie schon bei Stufe 1 und 2.
- **B, gehörte ACK-Kopie streichen:** Ein wartender `rx_ack_fwd`-Slot wird freigegeben, wenn dieselbe ACK-ID von einem Nachbarn gehört wird. Spart Sendungen und Ringplatz. Größerer Eingriff und Risiko, dass die Kopie nur eine Seite des Netzes erreicht hat; braucht eigenes Konzept.
- **C, nicht empfohlen:** Die zweite Gateway-Kopie früher senden. Sie soll Hörer erreichen, die die erste verpasst haben, und braucht dafür Abstand.

## Was nicht geprüft ist

- **nRF52 im Betrieb:** Kein Soak-Log eines nRF52-Knotens. Aussagen zu nRF52 sind Codelesung.
- **Frame-Zählung je Sendung:** Gezählt über `RX_FLAG_PROCESS` im Zeitfenster `wait=`. Frames, die nur als Preamble oder als CRC-Fehler endeten, sind nicht einzeln zugeordnet.
- **Sendungen ohne Empfang im Fenster:** `578805C9`, `04EC1298`, `09D938FD`, `0463496C` (6,7 s) wurden nur teilweise einzeln gelesen (`04EC1298` gelesen: CAD-Backoff).
- **Wirkung beim Absender:** Kein Test, ob und wann ein Absender wegen eines verspäteten ACK wiederholt. Die 40 s sind aus dem Quelltext-Kommentar übernommen, das 2-s-Raster nicht gegen den Aufrufer geprüft.
- **Andere Knoten:** Ob die Nachbarn dieselbe Firmware und dieselben Wartezeiten nutzen, ist unbekannt; das 3,4-s-Raster der Frames im Beispiel könnte aus gleichgetakteten Weiterleitungen stammen (Annahme).
- **Fixvorschlag A:** Weder gebaut noch auf dem Bench erprobt; die genannten 6 bis 8 s sind ein Ansatz, kein Messwert.
- **Auswertungsquelle:** Das Log ist eine Kopie im Scratchpad der Sitzung, nicht die Datei auf dem rpizero.
