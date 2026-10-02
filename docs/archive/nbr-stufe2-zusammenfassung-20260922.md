# Nachbarschaftsmatrix Stufe 2: Stand 2026-09-22

> **ARCHIVED 2026-09-23.** Betreiber-Zusammenfassung des Stufe-2-Konzepts vom 2026-09-22. Umgesetzt in den Wellen 0-6 von `../nbr-stage2-campaign.md`; Konzept live in `../nbr-wichtigkeit-konzept.md`.

Zusammenfassung für den Betreiber. Quellen: `docs/nbr-wichtigkeit-konzept.md` (Konzept v3),
`docs/nbr-stage2-campaign.md` (Kampagnenstand), `docs/nbr-logformat.md` (Logvertrag), Branch
`feature-neighbour-matrix`, Commits `11aaed61` (Firmware), `8966d03b` (Doku, Tools), `71f9d712`
(Kampagnenstand).

## 1. Kurzfassung

Das Ziel ist ein Flutnetz, das durch Mesh-Verkehr nicht zuläuft: weniger redundante Relays, ohne
eine Zustellung zu verlieren. Der Feldlauf hat gezeigt, dass das Problem nicht die Reihenfolge in
350-ms-Slots ist, sondern dass jeder Empfang den CSMA-Backoff neu aufsetzt und der Knoten mit den
meisten Nachbarn dadurch am längsten wartet (DL2JA-2: 109 s im Median). Festgelegt und gebaut ist
eine Sendeentscheidung je Frame aus der Matrix, die redundante Relays abbricht und unverzichtbare
vorzieht. Beide Testknoten laufen seit heute 09:28 damit im Zählmodus.

## 2. Was festgelegt wurde

| Thema               | Entscheidung                                                                                                                                                                                                                                                        |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Sendeentscheidung   | Je Frame, aus der eigenen Matrix. Bedarf = direkte Nachbarn, die den Frame noch nicht haben können. Fall A: mindestens einer bekommt ihn nur von mir. Fall B: alles, was ich erreiche, erreicht auch ein anderer, der den Frame hat. Kein Wissen: Fluten wie heute. |
| Fall A              | Vorrang: Basis 3,5 s (vor Relay/POS/HEY, hinter ACK), Slots 0 bis 2, nach 8 s Wartezeit nur noch Kurzsuche (150 ms Schutzabstand, dann CAD). Nie Abbruch.                                                                                                           |
| Fall B              | Nachrang: POS und HEY plus 20 s, Slots 7 bis 9, Text unverändert. Abbruch, sobald gehörte fremde Wiederholungen den Restbedarf decken.                                                                                                                              |
| Abbruch             | Nur gegen eine tatsächlich gehörte Aussendung, nur deckungsbasiert, nie bei Allein-Maske, nie beim Absender-Retry, nie beim eigenen Echo. Der bestehende Ring-Scan wurde dafür geöffnet, nichts neu erfunden.                                                       |
| Gateway-Ausnahme    | Gateways sind keine Abhängigen: der Server verteilt jeden Frame an jedes Gateway, abonnierte Gruppen stehen in dessen POS.                                                                                                                                          |
| Rang und Super-Node | Lokal berechenbar, keine Election, kein neues Frame, kein neues Feld. Für die Sendeentscheidung nicht nötig, nur Anzeige und Diagnose.                                                                                                                              |
| HN über HEY         | Gestrichen: jede heutige Firmware verwirft ein Ziel `HN` beim Dekodieren. Kopfbyte voll, HEY-Nutzlast verschiebt die Gruppenzuordnung.                                                                                                                              |
| Reichweite, NCT     | Keine Steuergrößen, nur Anzeige.                                                                                                                                                                                                                                    |
| Rollout             | Erst `count` (rechnen und zählen ohne Wirkung), dann `on`. Kill-Switch `--nbrrelay off`. Alles Konsolenkommandos, kein Reflash.                                                                                                                                     |
| Bench-Blatt         | Bench-Heltec DK5EN-1 mit 2 dBm als zweites Blatt, Mesh an, damit DK5EN-98 sieht, was das Blatt hört.                                                                                                                                                                |

## 3. Was implementiert ist

| Baustein                               | Inhalt                                                                                                                                                                         | Stand                      |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------- |
| Matrix-API (`src/nbr_matrix.*`)        | `nbrRelayNeed()` (Bedarf, Allein, known), `nbrCoverMask()`, `nbrRowMeshNeedCount()`, `nbrExclusiveDirect()`, Masken-Helfer                                                     | gebaut, 34 Host-Tests grün |
| Gateway-Echo-Fix                       | Spalte 0 schreibt nur noch der ME-Schritt beim Empfang. Vorher machte das Echo eines Serverframes den Absender zum direkten Nachbarn (OE1XAR-33 in 44 von 46 Schnappschüssen). | gebaut, Test               |
| Direktzeilen-Schutz                    | Verdrängung trifft 2-Hop-Zeilen vor frisch direkt gehörten Zeilen.                                                                                                             | gebaut, Test               |
| TX-Ring (`src/txring_functions.*`)     | `ringKind`, `ringNeed`, `ringAlone` je Slot, unter dem Lock gesetzt, beim Slot-Umzug mitkopiert                                                                                | gebaut, 27 Tests grün      |
| Sende-/Empfangspfad (`lora_functions`) | Masken beim Einreihen, `NEED`-Zeile, Mithör-Zweig mit `CANCEL`/`CANCEL?`/`REFUSE`, `csma_compute_timeout_slot()` mit Fall A/B unter `on`                                       | gebaut                     |
| Schalter                               | `--nbrrelay off\|count\|on` in `node_sset4` (0x0020, 0x0040), Boot-Restore auf ESP32 und nRF52, `--info`-Zeile mit fünf Zählern                                                | gebaut, 21 Tests grün      |
| Web-Seite `?page=neighbours`           | Vertikale Köpfe, Spalten `D/I G M #N #X Role` mit Hover und Legende, Rollenfarben, Umbruch und Sortierung, `Covered by`, Block "My relay decision"                             | gebaut, per HTTP geprüft   |
| Gate                                   | Builds Heltec V3, RAK4631, T-Beam, T-Deck Plus grün; String-Scan beider Images; Advisor-Pass APPROVED, ein Medium-Befund behoben (kein Wissen ist nicht Fall B)                | erledigt                   |
| Auswertung                             | `tools/nbrsnap.py` (Gerätesicht je Schnappschuss), `tools/nbrrelay.py` (Relay-Baseline aus `--setlog`), `tools/bench/serial_capture.py` (USB-Mitschnitt)                       | im Repo                    |

Nicht gebaut: Bitfeld für `#N`/`#X` jenseits der Zeilen (Anzeigeoption), Entfernungsfilter für
`#X` bei Gateway-Nachbarn, Matrix-Generationszähler gegen Maskenindex-Wiederverwendung nach einer
Verdrängung (Advisor, niedrig).

## 4. Testaufbau

| Knoten          | Rolle                         | Firmware                 | Konfiguration                                                                                   | Mitschnitt                                                                            |
| --------------- | ----------------------------- | ------------------------ | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| DK5EN-98        | Gateway und Mesh, Hauptknoten | `Sep 22 2026 / 09:00:32` | `--nbrrelay count`, nbrdebug, loradebug, txcapture, setlog                                      | rpizero, `~/meshlog/dk5en-98/2026-09-22.log`, 48 h ab 09:28:34                        |
| DK5EN-1 (Bench) | Blatt, 2 dBm, am Laptop       | `Sep 22 2026 / 09:00:32` | `--txpower 2`, `--mesh on`, `--gateway off`, `--nbrdebug on`, `--setlog on`, `--nbrrelay count` | Laptop, `~/meshlog/dk5en-1/2026-09-22.log`, `tools/bench/serial_capture.py` per nohup |

Was die Logs liefern: `NEED`-Zeilen je Relay mit Fall A, B oder U, `CANCEL?` für jeden Abbruch,
der im Zählmodus möglich gewesen wäre, `REFUSE` für jede verweigerte Deckung, dazu die
`--setlog`-Zeilen RX, RLY und TX wie im ersten Lauf. Die fünf Zähler stehen in `--info` und auf der
Web-Seite.

Beim Auswerten beachten:

- Der alte 24-h-Lauf endete 09:22 in derselben Tagesdatei; jede Auswertung braucht
  `--since 2026-09-22T09:28`.
- Die ersten Minuten zeigen auf DK5EN-98 nur Fall A, weil die Matrix nach dem Reboot noch keine
  Deckungskanten hat; das Blatt zeigt `U`, weil seine einzigen frischen Abhängigen Gateways sind.
  Erst nach Stunden urteilen.
- Der USB-Port des Blatts darf nicht geöffnet werden, das beendet den Mitschnitt und rebootet den
  Knoten. Die Konsole von DK5EN-98 gehört dem Logger; zum Eingreifen `PAUSE`-Datei anlegen.
- Zustellmessung R6: die msg_ids, die DK5EN-98 später abbricht, gegen den Empfang des Blatts
  abgleichen.

## 5. Was als Nächstes ansteht

| Schritt | Inhalt                                                                                                                                                                                                                              | Kriterium                                                                                                       |
| ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| 1       | Zählphase auf DK5EN-98 auswerten (`tools/nbrrelay.py`, `tools/nbrsnap.py`, `grep NEED`): Verteilung A/B/U, wie viele `CANCEL?`, gegen wen, wie oft `REFUSE`, und ob die neun Server-Rufzeichen aus der Direktheit verschwunden sind | mindestens ein Nachtlauf; `CANCEL?` deutlich über 0, `REFUSE` erklärbar, keine Serverabsender mehr als direkt   |
| 2       | `--nbrrelay on` auf DK5EN-98 (Konsole mit `PAUSE`), Blatt bleibt in `count`                                                                                                                                                         | Vorher/Nachher nach Konzept Abschnitt 8: eigene Relays je Stunde sinken, Wartezeit Fall A sinkt, R6 unverändert |
| 3       | Wenn R6 einen Rückgang zeigt oder ein `CANCEL` mit `alone != 0` auftaucht: sofort `--nbrrelay off`, Post-Mortem vor dem zweiten Versuch                                                                                             | jeder Rückgang ist ein Abbruchgrund                                                                             |
| 4       | Zweite Messstelle für die Latenz des Super-Nodes: Betreiber von DL2JA-2 ansprechen oder einen eigenen Knoten an einer Stelle mit vielen Nachbarn                                                                                    | 109 s Median bestätigt oder widerlegt, danach die Wirkung von Fall A darauf                                     |
| 5       | Web-Seite in Chrome anschauen (Erweiterung war heute nicht verbunden), Legende und vertikale Köpfe auf dem Telefon prüfen                                                                                                           | eine Korrekturrunde                                                                                             |
| 6       | Offene Kleinigkeiten: Entfernungsfilter für `#X` bei Gateway-Nachbarn, Matrix-Generationszähler, 24-h-Endauswertung des ersten Laufs                                                                                                |                                                                                                                 |
| 7       | Merge nach `fork-main` und Fork-Release, Release-Notes mit Kill-Switch-Hinweis                                                                                                                                                      | Schritte 1 bis 3 bestanden                                                                                      |
| 8       | Upstream in einzelnen PRs mit deutscher Beschreibung, Reihenfolge: Gateway-Echo-Fix und Direktzeilen-Schutz, dann Anzeige, dann Schalter und Zählmodus, zuletzt Abbruch und Backoff. Kurt reviewt und merged.                       | jeder PR für sich baubar und mit Host-Tests                                                                     |
| 9       | Beta-Cluster mit `on` auf mehreren Knoten, danach Flotte mit Default `off` und Freigabe per Kommando                                                                                                                                | Cluster-Metriken über zwei Wochen ohne Abbruchkriterium                                                         |

Der Gewinn skaliert mit dem Anteil neuer Knoten und braucht keine Koordination: ein Knoten mit
Stufe 2 bricht auch gegen die Kopien alter Firmware ab, und alte Firmware verhält sich unverändert.
