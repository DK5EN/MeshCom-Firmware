# Hardware-Leitlinien für MeshCom

**Vorschlag von DK5EN zur Diskussion mit OE1KBC und OE1KFR, nichts davon ist beschlossen. Ein neues Board kommt nur dazu, wenn es Kanal, Pflegeaufwand und Update-Weg nicht verschlechtert. Ein altes Board fällt erst weg, wenn kaum noch Knoten darauf laufen und das vorher angekündigt ist.**

Stand: 04.11.2026, Flottenzahlen von mcmap vom selben Tag.

## Kernfakten

- **Die Flotte läuft überwiegend auf klassischem ESP32:** 1055 von 1734 Knoten (61 %). Allein TLORA V2.1.6 stellt 566 Knoten (33 %).
- **ESP32-S3 holt auf:** 594 Knoten (34 %), davon Heltec V3 mit 364.
- **nRF52 ist klein, aber der einzige Weg zu wenig Strom:** 65 Knoten (4 %). Es gibt dort kein autonomes Auto Update.
- **Ältere Funkchips sind noch die Mehrheit:** etwa 973 Knoten (56 %) laufen nach Board-Zuordnung mit SX127x. Abgeleitet aus dem Boardnamen, nicht aus einer Abfrage des Chips.
- **Der Speicher begrenzt neue Funktionen auf klassischem ESP32 zuerst:** Heap ist dort der Engpass, und im 4-MB-Layout hat die Firmware einen App-Bereich von 3324 KB neben 704 KB Safeboot (`partitions-4MB-safeboot.csv`).

## Leitlinien

1. **Aufnahme nur mit Nutzen für das Netz oder die Nutzer.** Das Board bringt etwas, das die vorhandenen nicht können: Reichweite, Stromverbrauch, Bedienung oder Preis.
2. **Funkchip mit Zukunft.** Neue Boards haben SX126x oder neuer. SX127x wird weiter gepflegt, aber nicht neu aufgenommen.
3. **Kein Board ohne Update-Weg.** Web Flasher ist Pflicht. Auf ESP32 kommen Safeboot und Auto Update dazu, bevor das Board als unterstützt gilt.
4. **Mindestausstattung für Neuaufnahmen.** Vorschlag: ESP32-S3 mit mindestens 8 MB Flash und PSRAM, oder nRF52840. Die Werte sind eine Annahme und vor dem Beschluss gegen die Heap-Messungen zu prüfen.
5. **Testbar, bevor es unterstützt ist.** Jemand aus dem Team oder ein benannter Betreuer hat das Board in der Hand. Es läuft im CI-Build und hat eine Regel für die Board-Erkennung im Web Flasher.
6. **Sendeleistung und Band müssen zum Amateurfunk passen.** Das Board muss auf dem vorgesehenen Band ohne Eingriffe legal und oberwellenarm arbeiten. Für 1-W-Boards gehört dazu ein Hinweis zu Leistung und Antenne.
7. **Kaufbar.** Das Board ist bei mehr als einem Händler erhältlich und hat keine absehbare Abkündigung.
8. **Abkündigung nach festen Regeln.** Ein Board wird erst abgekündigt, wenn sein Anteil an der Flotte unter eine vereinbarte Schwelle fällt, und bekommt eine Frist mit Ankündigung. Vorschlag: unter 1 % und mindestens 12 Monate.

## Vorschlag für drei Stufen

| Stufe        | Bedeutung                                                          | Kandidaten                                                    |
| ------------ | ------------------------------------------------------------------ | ------------------------------------------------------------- |
| Voll         | Alle neuen Funktionen, Tests auf der Bench, Auto Update            | Heltec V3/V4, T-Beam Supreme, T-Deck Plus, RAK4631            |
| Erhalt       | Fehlerbehebung, Funktionen nur wenn der Speicher reicht            | TLORA V2.1.6, T-Beam V1.1/V1.2, E22, Heltec Stick und Tracker |
| Gemeinschaft | Baut im CI, Betreuung durch die Nutzer des Boards, kein Bench-Test | seltene Boards wie T-Connect Pro, Vision Master E213          |

Die Zuordnung ist ein erster Wurf. Sie muss vom Team kommen, nicht von dieser Liste.

## Was die anderen Projekte machen

- **Meshtastic führt Stufen:** Flagship, Niche und Legacy plus ein Kennzeichen für aktive Pflege. Klassischer ESP32 steht auf Legacy, der HTTP-Server wurde dort ab 2.7.21 aus Speichergründen abgekündigt.
- **Meshtastic rät zu neueren Funkchips:** SX126x oder LR11xx statt SX127x.
- **MeshCore nennt keine Stufen oder Kriterien:** Neue Boards kommen dem Anschein nach per Pull Request. Das ist abgeleitet aus den Release Notes.
- Hintergrund: [Marktanalyse](market-analysis-meshtastic-meshcore-20261104.md), Abschnitt Hardware.

## Offene Fragen

- **Wer entscheidet über Aufnahme und Abkündigung?** Heute gibt es dafür keine benannte Stelle.
- **Wie lange bleibt klassischer ESP32 in der Stufe Erhalt?** Bei 61 % der Flotte ist das die wichtigste Einzelfrage. Sie hängt mit Signaturen, Arduino 3.x und zweitem App-Slot zusammen.
- **Welche Funktionen dürfen nur auf S3 und nRF52840 laufen?** Ein Vorschlag fehlt noch. Ein Kandidat ist die Signaturprüfung, wenn die Messung auf klassischem ESP32 schlecht ausfällt.
- **Bekommen neue Plattformen wie nRF54 oder ESP32-C6 eine eigene Prüfung?** Nichts davon wurde bisher auf MeshCom-Eignung geprüft.

## Was nicht geprüft ist

- **Momentaufnahme:** Die Abfrage zeigt nur Knoten, die in den letzten rund 1,5 Tagen gehört wurden. Länger stille Knoten fehlen.
- **Nur gemeldete Hardware:** 20 Knoten melden keine Hardware. Selbstbauten mit gleicher Kennung sind nicht unterscheidbar.
- **Mindestausstattung:** Die Werte in Leitlinie 4 und die Schwelle in Leitlinie 8 sind Vorschläge ohne Messung.

<details>
<summary>Flottenzahlen je Hardware vom 04.11.2026</summary>

Quelle: `nodes_query` der mcmap, 1734 Knoten, abgeglichen mit `fleet_firmware` je Build. "Unter 4.40a" zählt Knoten mit älterer Firmware.

| Hardware                  | Knoten | Anteil | Unter 4.40a |
| ------------------------- | ------ | ------ | ----------- |
| TLORA V2.1.6              | 566    | 32,6 % | 220         |
| HELTEC V3                 | 364    | 21,0 % | 120         |
| TBEAM V1.2                | 265    | 15,3 % | 76          |
| TBEAM V1.1                | 126    | 7,3 %  | 55          |
| EBYTE E22                 | 73     | 4,2 %  | 20          |
| T-DECK +                  | 48     | 2,8 %  | 17          |
| RAK4631                   | 44     | 2,5 %  | 10          |
| T-BEAM 1W                 | 40     | 2,3 %  | 8           |
| HELTEC STICK V3           | 34     | 2,0 %  | 9           |
| HELTEC Tracker            | 31     | 1,8 %  | 8           |
| TBEAM SUPREME             | 23     | 1,3 %  | 6           |
| ohne Angabe               | 20     | 1,2 %  | 0           |
| HELTEC E290               | 18     | 1,0 %  | 4           |
| HELTEC V2.1               | 16     | 0,9 %  | 5           |
| HELTEC V4                 | 15     | 0,9 %  | 3           |
| T-ECHO                    | 14     | 0,8 %  | 1           |
| T-DECK                    | 9      | 0,5 %  | 1           |
| Heltec Wireless Paper     | 8      | 0,5 %  | 0           |
| HELTEC T114               | 7      | 0,4 %  | 0           |
| TBEAM V1.1 1268           | 6      | 0,3 %  | 4           |
| TBEAM V1.2 1262           | 3      | 0,2 %  | 1           |
| HELTEC Vision Master E213 | 2      | 0,1 %  | 0           |
| ESP32-S3 E22              | 1      | 0,1 %  | 0           |
| T-CONNECT-PRO             | 1      | 0,1 %  | 0           |

Einteilung der Chipfamilie: klassischer ESP32 sind TLORA, TBEAM V1.x, EBYTE E22 und Heltec V2.1. nRF52 sind RAK4631, T-Echo und T114. Der Rest ist ESP32-S3.

</details>
