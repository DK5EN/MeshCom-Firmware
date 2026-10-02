# PN-Wiederholung per XOR — Kollisionsanalyse gegen Produktionsdaten

- Stand: 27.09.2026, DK5EN
- Bezug: `pn-zustellung-dedup.md` Kapitel 4.3 (Variante a, XOR-Form), Branch `dk5en-xor`
  (`032fee8e`) in `MeshCom-Firmware-DEV-Main`
- Datenbasis: Snapshot der meshmap-Produktions-DB (`oe-link`, `/var/lib/mcmap/meshcom-wss.db`,
  `VACUUM INTO`, 27.09. 14:17 CEST)

## 0. Kurzfassung

1. **9 Rufzeichenpaare** kollidieren mit der XOR-Form auf Bit 30–31: ihre Knotenkennungen sind in
   den unteren 20 Bit gleich und unterscheiden sich nur in Bit 20–21 (Tabelle in Kapitel 2).
2. **Keine einzige tatsächliche Kollision in 40 Tagen**: keine msg_id ⊕ k (k = 1..3 in Bit 30–31)
   trifft eine andere Meldung, weder im 38-min- noch im 12-h-Fenster noch über den ganzen Zeitraum.
3. **Die Kostenrechnung in 4.3 ist um den Faktor ~4 zu optimistisch.** ESP32-MACs werden in
   Viererblöcken vergeben, die untersten 2 Bit der Knotenkennung sind bei 97 % der Knoten `00`.
   Effektiv bleiben heute ~20 Bit, mit der 30-Bit-Maske ~18 Bit — erwartet ~6,5 Paare, beobachtet 9
   (4.3 rechnet mit 1,07).
4. **Der Zähler läuft nicht in 12 h um**, sondern im Median in ~8 Tagen (schnellste 10 %: ~3 Tage).
   Maßgeblich ist nicht der Umlauf, sondern das Dedup-Fenster.
5. **Alternative Bit 10–11** (die untersten 2 Bit der Knotenkennung) statt Bit 30–31: bei ESP32
   ohnehin immer `00`, damit **1 statt 9** Kollisionsgruppen — mit einer Auflage für den Server
   (Kapitel 5).
6. **00226E, 002237 und 01D5B7 gehören keinem Knoten.** Es sind fortlaufende 32-Bit-Zähler des
   Servers, die zufällig im 22-Bit-Präfix zusammenfallen: 00226E und 002237 für die per UDP
   gemeldeten Eigenpositionen der Gateways, 01D5B7 für Textmeldungen über `BOT GATE` (Kapitel 6).

## 1. Datenbasis und Methode

- 14.155 Textmeldungen (Gruppen, `*` und PN) aus INTERLINK, 18.08. 12:48 – 27.09. 14:11 CEST
- Knotentabelle: 1.593 Rufzeichen, davon 1.567 mit letzter msg_id (letzter pos/hey-Frame)
- Daraus 1.843 verschiedene 22-Bit-Knotenkennungen (`msg_id >> 10`) zu 1.917 Rufzeichen
- Alle 14.155 Zählerwerte (`msg_id & 0x3FF`) liegen in 0..999 — das Format
  `((_GW_ID & 0x3FFFFF) << 10) | node_msgid` gilt für die Textmeldungen durchgängig
- Einschränkung: Positionen, HEY und Telemetrie liegen nur als jeweils letzte msg_id pro Knoten vor,
  nicht als Verlauf. Die empirische Prüfung in Kapitel 3 deckt daher nur Text gegen Text ab.

## 2. Kollidierende Paare mit XOR auf Bit 30–31

Gleiche untere 20 Bit der Knotenkennung, Unterschied nur in Bit 20–21 (= msg_id-Bit 30–31). Eine
Wiederholung des einen Knotens trägt damit im Dedup-Schlüssel `id & 0x3FFFFFFF` dieselbe Kennung
wie der andere.

| Untere 20 Bit | Kennung A | Rufzeichen A | Kennung B | Rufzeichen B |
| ------------- | --------- | ------------ | --------- | ------------ |
| 17ED0         | 217ED0    | DL5WB-10     | 317ED0    | IU4KVZ-10    |
| 17F4C         | 117F4C    | IS0LBH-18    | 317F4C    | DL1LAA-2     |
| 33580         | 033580    | DO3AC-1      | 133580    | OE5SRL-12    |
| 47AB0         | 147AB0    | DF9BU-13     | 347AB0    | DD0AO-02     |
| 8DEE4         | 18DEE4    | DL8BZ-11     | 28DEE4    | OE5SRL-1     |
| 94C78         | 094C78    | DO7DH-23     | 294C78    | HB9JAY-6     |
| 94FA0         | 294FA0    | IU5PMR-15    | 394FA0    | DK8VW-55     |
| A49E0         | 1A49E0    | DL2GKA-12    | 2A49E0    | SQ9RFE-1     |
| BCE40         | 0BCE40    | DJ2AX-55     | 3BCE40    | DL2FH-10     |

Keine der drei Server-Kennungen aus Kapitel 6 bildet mit einer MAC-Kennung ein solches Paar.

## 3. Empirische Prüfung

Für jede Meldung: kommt `msg_id ⊕ (k << 30)` mit k = 1, 2, 3 als andere Meldung vor?

| Fenster                     | Treffer |
| --------------------------- | ------- |
| 38 min (Dedup-Ring Knoten)  | 0       |
| 12 h                        | 0       |
| gesamter Zeitraum (40 Tage) | 0       |

Zum Vergleich: identische 32-Bit-msg_id von verschiedenen Absendern innerhalb 12 h — ebenfalls 0.

## 4. Warum mehr Paare als gerechnet, und wie schnell der Zähler läuft

**Entropie der Knotenkennung.** `getMacAddr()` (`src/esp32/esp32_main.cpp:565`) legt die MAC-Bytes
3–5 in die unteren 22 Bit. Espressif vergibt pro Chip vier aufeinanderfolgende MACs (WLAN-STA, AP,
BT, ETH), die Basis-MAC ist daher durch 4 teilbar.

| Untere 2 Bit der Knotenkennung | Kennungen |
| ------------------------------ | --------- |
| `00`                           | 1.784     |
| `01`                           | 23        |
| `10`                           | 17        |
| `11`                           | 19        |

Die obersten 2 Bit (20–21) sind dagegen gleichmäßig verteilt (369 / 619 / 465 / 390). Folge:

| Schlüssel                        | effektive Bit | erwartete Paare bei 1.843 Kennungen |
| -------------------------------- | ------------- | ----------------------------------- |
| heute, 22 Bit                    | ~20           | ~1,6                                |
| XOR auf Bit 30–31, 20 Bit        | ~18           | ~6,5 (beobachtet 9)                 |
| Rechnung in 4.3 (gleichverteilt) | 20            | 1,07                                |

**Zählerumlauf.** Aus aufeinanderfolgenden Meldungen desselben Knotens (Abstand 10 min – 6 h,
n = 5.744):

| Perzentil | Schritte/h | Umlauf 1.000 Werte |
| --------- | ---------- | ------------------ |
| p10       | 2,5        | ~17 Tage           |
| Median    | 5,1        | ~8 Tage            |
| p90       | 13,7       | ~3 Tage            |

Der Zähler wird im Flash gehalten (`src/esp32/esp32_flash.cpp:107`, `:398`). Eine msg_id ist pro
Knoten also über Tage eindeutig, nicht nur über 12 h. Für eine Fehlerkennung zählt das
Dedup-Fenster: beide Knoten eines Paares müssen darin denselben Zählerwert senden.

**Größenordnung pro PN eines betroffenen Knotens** (Partner mit Median-Rate):

- Knoten-Ring (~38 min): ~0,3 %. Zusätzlich muss der Empfänger beide Knoten hören.
- Server mit 12-h-Fenster: ~6 %. Der Server sieht das ganze Netz, räumliche Entfernung hilft nicht.
  Folge wäre eine echte PN, die als Wiederholung verworfen wird.

## 5. Alternative: Wiederholungszähler in Bit 10–11

Bit 10–11 der msg_id sind die untersten 2 Bit der Knotenkennung — bei ESP32 immer `00`.

| Variante          | Kollisionsgruppen im Bestand                         |
| ----------------- | ---------------------------------------------------- |
| XOR auf Bit 30–31 | 9 (Kapitel 2)                                        |
| XOR auf Bit 10–11 | 1: 00226D (SQ5STR-6) gegen die Server-Kennung 00226E |

- Erstsendung bleibt byte-gleich, Relais unverändert — wie bei Bit 30–31.
- Maske `id & ~(3u << 10)` statt `id & 0x3FFFFFFF`; Eigenknotentest
  `(id >> 12) == ((_GW_ID & 0x3FFFFF) >> 2)`.
- Die ~3 % nRF52-Knoten (MAC mit voller Entropie) verlieren 2 echte Bit, die ESP32-Knoten keins.
- **Auflage Server**: die Server-Zähler aus Kapitel 6 laufen durch alle unteren Bit. Der
  Gateway-Positions-Zähler erreicht `X + 1024` (= X ⊕ Bit 10) nach ~3–9 h. Maskiert der Server
  auf Bit 10–11 in einem Fenster dieser Länge, hält er fremde Gateway-Positionen für
  Wiederholungen. Der Server darf die Maske deshalb **nur auf PN** anwenden (Text mit `{NNN` an
  persönliches Ziel) — dasselbe gilt für die Firmware, die das im Branch bereits so macht.
  Bei Bit 30–31 stellt sich die Frage nicht: der Zähler bräuchte 2^30 Schritte.

## 6. Die Server-Kennungen 00226E, 002237 und 01D5B7

Keine der drei ist eine Knotenkennung. Belege:

- Die msg_ids steigen **über verschiedene Rufzeichen hinweg** in zeitlicher Reihenfolge fortlaufend
  an, z. B. 0088DF00 (DO1NAE-9) → 0088DF04 (DL5WB-10) → 0088DF05 (DK4YU-77) → 0088DF06 (DL6LC-12).
- Zählerwert **1.000** bei DL6LC-12 (0088DFE8) — die Firmware kommt nie über 999.
- Im INTERLINK-Rohlog sind es `pos`-Frames mit `gw:1`, `rssi:0`, `snr:0`: die Eigenposition, die ein
  Gateway per UDP an den Server meldet. Der Server vergibt dafür eine eigene msg_id.
- Dieselben Gateways senden über LoRa weiterhin mit ihrer MAC-Kennung (DL5WB-10 = 217ED0 in
  Kapitel 2). In der Knotentabelle steht die Server-Kennung nur, wenn der letzte Frame eines
  Gateways dieser UDP-Frame war — 38 von 775 Gateways zum Zeitpunkt des Snapshots.
- Es laufen zwei Zähler parallel mit verschiedenem Tempo: 0088DFxx (~120/h) und 0089B9xx (~390/h),
  vermutlich je Server-Instanz bzw. Hub (nicht verifiziert). 00226E und 002237 sind nur die
  22-Bit-Präfixe, in denen diese Zähler gerade stehen — sie wandern weiter.

### 6.1 00226E — Gateway-Eigenpositionen, Zähler 0089B9xx (27 Gateways)

| Rufzeichen | Hardware        | Firmware |
| ---------- | --------------- | -------- |
| DA1UR-12   | TBEAM V1.2      | 4.35t    |
| DB0BT-20   | HELTEC V3       | 4.35k    |
| DC6BV-7    | TBEAM V1.2      | 4.35f    |
| DF8RJ-12   | TBEAM V1.1      | 4.35h    |
| DG3YBH-12  | TBEAM V1.2      | 4.35t    |
| DG6NGK-12  | EBYTE E22       | 4.35t    |
| DG8OB-2    | HELTEC V3       | 4.35h    |
| DL1VED-99  | TBEAM V1.1      | 4.35t    |
| DL4MDI-12  | TBEAM SUPREME   | 4.35n    |
| DL4OCE-3   | HELTEC Tracker  | 4.35t    |
| DL7AOD-1   | T-DECK          | 4.35p    |
| DM5TU-99   | TBEAM V1.1      | 4.35t    |
| DN9DPA-01  | TBEAM SUPREME   | 4.35k    |
| DO1EMC-12  | TBEAM V1.2      | 4.35i    |
| DO7AGN-12  | TBEAM SUPREME   | 4.35t    |
| DO9JTR-1   | T-DECK +        | 4.35h    |
| F4MXR-9    | TBEAM V1.2      | 4.35t    |
| F5UJA-7    | TBEAM V1.2 1262 | 4.35f    |
| G1IVG-12   | EBYTE E22       | 4.35f    |
| HB9EHP-41  | TBEAM V1.2      | 4.35h    |
| HB9HZW-3   | T-BEAM 1W       | 4.35t    |
| HF0TIR-1   | TBEAM V1.2      | 4.35t    |
| OE6BME-15  | T-BEAM 1W       | 4.35p    |
| OE7WPA-12  | TBEAM V1.1      | 4.35p    |
| S51BX-13   | HELTEC V3       | 4.35f    |
| S51D-7     | TBEAM SUPREME   | 4.35f    |
| SP6ZLT-10  | HELTEC Tracker  | 4.35t    |

Hardware: TBEAM V1.2 8, TBEAM V1.1 4, TBEAM SUPREME 4, HELTEC V3 3, HELTEC Tracker 2, EBYTE E22 2,
T-BEAM 1W 2, T-DECK 1, T-DECK + 1 — alles ESP32, Firmware 4.35f bis 4.35t.

### 6.2 002237 — Gateway-Eigenpositionen, Zähler 0088DFxx (9 Gateways)

| Rufzeichen | Hardware      | Firmware |
| ---------- | ------------- | -------- |
| DB4SJP-12  | TBEAM V1.2    | 4.35p    |
| DB7GV-15   | TBEAM V1.1    | 4.35o    |
| DL4YM-12   | TBEAM V1.2    | 4.35p    |
| DL6LC-12   | TBEAM SUPREME | 4.35p    |
| DL9GCW-3   | EBYTE E22     | 4.35d    |
| DN9DZ-2    | T-DECK +      | 4.35p    |
| DO1KMA-2   | TBEAM V1.2    | 4.35p    |
| DO1MSG-99  | HELTEC V3     | 4.35o    |
| DO7ABM-12  | EBYTE E22     | 4.35p    |

Hardware: TBEAM V1.2 3, EBYTE E22 2, TBEAM V1.1 1, TBEAM SUPREME 1, T-DECK + 1, HELTEC V3 1.

### 6.3 01D5B7 — Textmeldungen über `BOT GATE` (20 Absender)

- 144 Meldungen, msg_id 0756DD1C … 0756DDC2 fortlaufend (166 Zählerschritte für 144 Meldungen, die
  Lücken sind vermutlich Meldungen, die wir nicht sehen), 19.08. – 27.09., alle mit
  `via = BOT GATE`, 143 davon über den `oe`-Hub. Der Zähler läuft sehr langsam (~4 pro Tag).
- Die Meldung wird serverseitig eingespeist, nicht von einem Funkgerät erzeugt. Die Hardware des
  Absenders ist dafür ohne Belang; nur 2 der 20 Rufzeichen stehen überhaupt in der Knotentabelle.

| Absender  | Meldungen | Hardware (falls als Knoten bekannt) |
| --------- | --------- | ----------------------------------- |
| OE1KBC-1  | 48        | —                                   |
| DL4QB-90  | 34        | —                                   |
| OE3WAS    | 25        | —                                   |
| OE5HWN-6  | 6         | —                                   |
| IU5CZN-00 | 5         | —                                   |
| DL2XL     | 4         | —                                   |
| IU5CZN-4  | 3         | —                                   |
| DG3FBL-99 | 2         | —                                   |
| HB9JAO    | 2         | —                                   |
| IU5CZN-3  | 2         | —                                   |
| OE1EZA    | 2         | —                                   |
| OE6KHD    | 2         | —                                   |
| OE6POD    | 2         | —                                   |
| DG7FDL-15 | 1         | —                                   |
| DK9SO-1   | 1         | —                                   |
| DL5WB-9   | 1         | TBEAM V1.1, 4.35t                   |
| DO1TFS    | 1         | —                                   |
| DO2THM-12 | 1         | HELTEC V3, 4.35p                    |
| HB9WNM    | 1         | —                                   |
| OE1KFR-9  | 1         | —                                   |

**Bedeutung für die PN-Wiederholung:** keine direkte. Die Server-Zähler erreichen die Varianten
X ⊕ k in Bit 30–31 nie (Kapitel 5). Sie verfälschen aber jede Statistik "Kennung → Rufzeichen"
und müssen bei Auswertungen wie dieser herausgefiltert werden. Zu klären bleibt, ob der Server
diese msg_ids an Gateways zurückgibt und sie damit ins LoRa-Netz gelangen.

## 7. Offene Fragen

1. Bit 30–31 (wie im Branch) oder Bit 10–11? Bit 10–11 hätte hier 1 statt 9 Kollisionen, braucht
   aber die PN-only-Maske auch im Server.
2. Welches Dedup-Fenster hat der zentrale Server? Davon hängt die ~6-%-Abschätzung ab.
3. Gelangen die Server-msg_ids (Gateway-Eigenpositionen, `BOT GATE`) per Gateway ins LoRa-Netz?
4. Zwei parallele Server-Zähler (0088DFxx, 0089B9xx): eine Instanz pro Hub?
