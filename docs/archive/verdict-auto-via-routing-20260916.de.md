# Bewertung: Automatisches VIA-Routing aus HEY/PATH-Daten (Discussion #1146)

Datum 2026-09-16. Anlass: [icssw-org/MeshCom-Firmware Discussion #1146](https://github.com/icssw-org/MeshCom-Firmware/discussions/1146)
von s-m-arty (14:13 UTC), beantwortet von OE1KBC um 15:28 UTC ("still having internal
discussions about the use of HEY PATH and also about using a new message ID ... leaning
towards keeping the existing message ID and using an additional sequential numbering system").
Jede Codezeile unten ist gegen `fork-main`/`dry-unification` und `upstream/dev` (`4058b25b`,
2026-09-14) geprüft; jede Feldzahl stammt aus den Quellen in Abschnitt 9. Die englische
Fassung liegt in `docs/verdict-auto-via-routing-20260916.md`.

Teil A (Abschnitte 1 bis 6) ist die Bewertung des Routing-Vorschlags. Teil B (Abschnitt 7) ist
die Bewertung der Meshmap-Frage zu den Gateway-Links, die eine andere bindende Ursache hat als
den Dedup-Ring. Abschnitt 8 nennt die Fragen, deren Antworten hier etwas ändern würden.

---

## 1. Kurzantwort

### 1.1 Für die Discussion (Englisch, 5 Sätze)

The building blocks are real, but the PATH table is a trace of where one HEY beacon happened
to arrive, not a route: it holds only relayed HEYs, keeps the shortest path of the last 12
hours rather than the freshest, and knows nothing about the reverse direction, which in the
field differs from the forward direction by 5 to 12 dB. Today's firmware also strips the VIA
list at the first relay, so a multi-hop source route first needs new relay behaviour in the
whole fleet, and the one AUTO-VIA retry with the same msg_id runs into the same dedup defect
as today's 40 s retry (every node that heard the first attempt drops it, including the
destination). A chain of named relays multiplies loss and sums queue latency (30 s median,
286 s p99 per hop at a loaded site) where the flood takes the fastest of many relays. DMs are
under one percent of frames, so the airtime prize is invisible while the risk sits on the one
traffic class users watch. We would keep the goal and reach it differently: a soft relay
preference on top of the flood (named or well-connected relays go first, the rest only if no
copy was heard) plus the two-identifier ARQ (fresh msg_id per retransmission, stable sequence
number) that our DM transport work already implements.

### 1.2 Für das Gespräch mit Kurt (Deutsch, 5 Sätze)

Die Bausteine gibt es, aber die PATH-Tabelle ist eine Spur, wo eine HEY-Bake zufällig
angekommen ist, keine Route: sie enthält nur relayte HEYs, behält zwölf Stunden lang den
kürzesten statt den frischesten Pfad und weiß nichts über die Gegenrichtung, die im Feld 5 bis
12 dB anders ist. Die heutige Firmware wirft die VIA-Liste beim ersten Relay weg, ein
Mehrhop-Source-Routing braucht also zuerst neues Relay-Verhalten in der ganzen Flotte, und der
eine AUTO-VIA-Retry mit derselben msg_id läuft in denselben Dedup-Fehler wie der heutige
40-s-Retry (jeder Knoten, der den ersten Versuch gehört hat, verwirft ihn, auch das Ziel).
Eine Kette benannter Relais multipliziert Verluste und summiert Warteschlangen (30 s Median,
286 s p99 je Hop an einem belasteten Standort), während die Flut den schnellsten von vielen
Relais nimmt. DMs sind unter einem Prozent der Frames, der Airtime-Gewinn ist also unsichtbar,
das Risiko sitzt genau auf der Verkehrsklasse, auf die Nutzer schauen. Wir behalten das Ziel
und erreichen es anders: weiche Relay-Bevorzugung auf der Flut (benannte oder gut vernetzte
Relais zuerst, alle anderen nur, wenn keine Kopie gehört wurde) plus die Quittung mit zwei
Kennungen (frische msg_id je Wiederholung, stabile Sequenznummer), die unsere
DM-Transportsicherung schon implementiert.

Zu Kurts Satz "bestehende msg_id behalten plus Sequenznummer": entscheidend ist, welche der
beiden Kennungen bei einer Wiederholung neu ist, sonst schluckt jeder Altknoten den Retry
(Abschnitt 6).

### 1.3 Gesprächsleitfaden (17 Punkte)

Die eigene Siebenerliste des Betreibers, geprüft und ergänzt. Die Abschnittsverweise führen
zur Evidenz.

**Warum die PATH-Tabelle keine Route tragen kann**

1. **Der HEY-Pfad ist die Empfangsrichtung, wir wollen senden, und die Verbindungen sind
   unsymmetrisch.** Ein Pfad hält fest, wer wen gehört hat, mit dem RSSI des Empfängers;
   Umkehren setzt Symmetrie voraus. Gemessen in Freising: 5 bis 12 dB zwischen den beiden
   Richtungen (DC2MAC-1/DL2JA-2: 11,6 dB). Bei SF11 ist das der Unterschied zwischen einem Link,
   der geht, und einem, der nicht geht (3.2).
2. **Die Tabelle behält den kürzesten Pfad der letzten 12 Stunden, nicht den frischesten.** Ein
   2-Hop-Pfad von vor 11 Stunden blockt einen frischen 3-Hop-Pfad. Und sie enthält nur relayte
   HEYs, direkte Nachbarn stehen nie drin: eine DM an den Nachbarn hat gar keine "Route" (3.1).
3. **Die heutige Firmware wirft die Mehrhop-VIA-Liste beim ersten Relay weg.** Ab Hop 2 flutet
   alles. Die Liste zu bewahren ist eine Änderung im Relay-Pfad aller 1.449 meldenden Knoten,
   762 davon auf 4.35p; der gemischte Übergang ist das Risiko, nicht der Absender (3.3).
4. **Es ist Source-Routing: hört ein benanntes Relay nicht oder kommt nicht auf Sendung, ist
   das Paket weg.** Nicht nur Gateways, jedes Relay in der Kette. Die Kette summiert
   Warteschlangen (30 s Median, 286 s p99 je Hop am belasteten Standort), die Flut nimmt das
   schnellste von 2 bis 4 Relais. Die Redundanz der Flut ist genau das, was der Vorschlag
   ausgibt (3.4).
5. **Das Beibehalten der msg_id lässt den Retry im Dedup-Filter sterben.** Jeder Knoten, der
   den ersten Versuch gehört hat, verwirft ihn, auch das Ziel, eine verlorene Quittung kann so
   nie repariert werden. Der 40-s-Timer feuert vor der Umlaufzeit (1 min ruhig, 4 min Median
   belastet), die Route wird aus einem Timing-Grund gesperrt, und die Flut mit neuer msg_id kommt
   obendrauf: VIA plus Flut, mehr Airtime als die Flut allein (3.5).
6. **Die Tabellengröße ist je Board verschieden und läuft heute über.** 100 (S3, nRF52), 40
   (klassischer ESP32, 64 % der Flotte), 10 (eine Klasse). Rundum-Überschreiben; ein gut
   platzierter Knoten sieht in 12 Stunden weit mehr Origins (3.1).
7. **Keine Persistenz, nur RAM.** SD-Karte nur am T-Deck. Nach jedem Neustart ist die Tabelle
   leer und füllt sich im Trickle-Takt (bis 15 min je Origin), während Trickle 29 bis 75 % der
   HEYs unterdrückt (3.1).

**Was sonst noch dagegen spricht**

8. **Der Gewinn ist unsichtbar.** DMs sind unter 1 % der Frames (1.182 in 7 Tagen gegen etwa
   3.800 HEY und 3.800 Positionen je Stunde). Die Netz-Airtime ändert sich um eine unmessbare
   Zahl; das Risiko sitzt auf dem einen Frame, dessen Verlust der Nutzer sieht (3.6).
9. **Die Quittung flutet trotzdem.** `:ackNNN` ist ein normaler Text-Frame; die Ersparnis liegt
   nur auf dem Hinweg (3.4).
10. **Die VIA-Liste kostet Bytes.** Etwa 20 Byte je benanntem Hop, rund 0,24 s bei SF11 je
    Kopie (3.6).
11. **Gateways fallen raus.** Ein Gateway, das nicht in der Liste steht, relayt nicht, und das
    sind meist die Knoten mit den besten Standorten. Der Upload zum Server läuft weiter, das ist
    der gute Teil (3.7).
12. **Kurt hat es selbst probiert.** Der mheard-basierte Auto-VIA-Zweig war im Code und wurde am
    2026-07-22 "zum Test entfernt" (`c47e4993`). Zu fragen, warum, ist die beste Eröffnung
    (3.1, 5).
13. **Zwei kleine Defekte im heutigen Gatter:** Substring-Vergleich (`OE1KBC-7` matcht in
    `OE1KBC-71`) und eine ungeordnete Liste, jeder benannte Knoten relayt, sobald er hört (3.3).
14. **Routenzustand kostet RAM, wo keiner ist:** der klassische ESP32 hat etwa 6,6 kB Reserve
    (3.5).

**Was stattdessen anzubieten ist**

15. **Weiche Relay-Bevorzugung statt harter Routen.** Die Flut bleibt; benannte oder gut
    vernetzte Relais bekommen den frühen CSMA-Slot, alle anderen den späten und stornieren, wenn
    sie vorher eine Kopie hören. Gleiche Ersparnis im guten Fall, volle Redundanz im schlechten,
    keine Wire-Änderung, Altknoten fluten wie heute (4.2).
16. **Die eine Kennungsfrage.** Kurt sagt "msg_id behalten, Sequenznummer dazu". Entscheidend
    ist, welche der beiden bei einer Wiederholung neu ist. Frische msg_id plus stabile
    Sequenznummer funktioniert mit jedem Altknoten, und die Sequenznummer gibt es schon als
    `{NNN`. Gleiche msg_id mit neuer Sequenznummer wird von jedem Altknoten geschluckt, bis die
    ganze Flotte umgestellt ist (6).
17. **Vorher messen.** Niemand misst heute, ob DMs ankommen. Stufe 0 der DM-Transportsicherung
    bringt die Zähler (gesendet, Echo, Gateway-ACK, Peer-ACK, aufgegeben). Ohne Vorher/Nachher
    lässt sich kein PoC beurteilen (4.3).

---

## 2. Was der Vorschlag richtig macht

- **Disziplin im Umfang.** Nur DMs, kein neues Funkprotokoll, Flut als Rückfall, Broadcasts
  unangetastet. Das ist die richtige Form für alles, was den Relay-Pfad berührt.
- **Die Dedup-Konsequenz ist gesehen.** "A new msg_id for the flood fallback is necessary
  because deduplication is based on the message ID" ist genau die Ursache unseres Defekts 1 in
  `docs/proposal-dm-transport-reliability-20260909.md` Abschnitt 2. Das erkennen wenige.
- **Die Bausteine existieren.** VIA-Gatter (`src/via_functions.cpp`), PATH-Tabelle
  (`src/mheard_functions.cpp`, `updateHeyPath()`), Ende-zu-Ende-Quittung (`{NNN` / `:ackNNN`),
  Wiederholung durch den Absender. Nichts muss erfunden werden, um es zu probieren.
- **Wo es sich lohnen würde.** In einem dichten städtischen Cluster ist jedes Relay einer DM
  redundant (siehe `docs/prio-talk-flood-networking.md`, Szenario "vollvermascht"): eine Flut
  kostet 2 bis 5 Sendungen je Hop-Ebene, eine Source-Route eine. Auf einem Kanal mit 66 %
  CAD-belegt ist das für diese eine Nachricht eine echte lokale Ersparnis.
- **Routenqualität aus RSSI, SNR, Hop-Zahl und ACK-Historie** ist der richtige langfristige
  Eingang. Es ist genau das, was unser "Ameisenpfad"-Vorschlag (Deck-Folie 15, "Der Vorschlag:
  Routing") in die Relay-Priorität statt in die Pfadwahl speisen will.

---

## 3. Warum Source-Routing aus der PATH-Tabelle nicht trägt

### 3.1 Die Tabelle ist eine Spur, keine Route

Was `updateHeyPath()` tatsächlich speichert (`src/mheard_functions.cpp:460-640`):

| Eigenschaft                    | Code                                                                   | Folge für Routing                                                                        |
| ------------------------------ | ---------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Nur relayte HEYs               | `if(ips <= 0) return;` (kein Komma im Quellpfad heißt kein Eintrag)    | Direkte Nachbarn stehen nie in der Tabelle. Eine DM an einen Nachbarn hat keine "Route". |
| Schlüssel ist der Origin       | ein Slot je HEY-Ursprungsrufzeichen                                    | Ein Pfad je Ziel, keine Alternativen, kein Zustand je Link.                              |
| Kürzester Pfad gewinnt         | `if((mheardPathLen[ipos] & 0x7F) < mheardLine.mh_path_len) return;`    | Ein 2-Hop-Pfad von vor 11 h schlägt einen 3-Hop-Pfad von vor 1 min.                      |
| Alterung 12 h                  | `MHEARD_PRUNE_WINDOW_MS`, geprüft nur beim Eintreffen eines HEY        | "Frisch" im Vorschlag muss erst definiert werden; heute heißt es "unter 12 h".           |
| Pfadfeld 51 Zeichen            | `mheardPathBuffer1[MAX_MHPATH][52]`                                    | Etwa fünf Rufzeichen; längere Ketten werden stumm abgeschnitten.                         |
| Größe je Board                 | `MAX_MHPATH` 100 (S3, nRF52), 40 (klassischer ESP32), 10 (eine Klasse) | Der klassische ESP32 ist 64 % der Flotte und hält 40 Ziele.                              |
| Keine Persistenz               | SD-Karte nur am T-Deck                                                 | Jeder Neustart beginnt mit leerer Tabelle.                                               |
| Gespeist aus Trickle-HEYs      | `TRICKLE_IMIN_S` 30 s bis `TRICKLE_IMAX_S` 15 min, k = 2               | Ein Knoten unterdrückt 29 bis 75 % seiner eigenen HEYs (`docs/hey-supp.md`).             |
| `--shortpath`-Knoten           | Quellpfad wird vor dem Relay auf "Origin, letztes Relay" gekürzt       | Diese Pfade lassen sich nicht umkehren; die Tabelle kann sie nicht unterscheiden.        |
| Nie von der Sendelogik gelesen | alle Leser sind Konsole, T-Deck-Display, WebGUI, SD (Deck-Folie 8)     | Kurts eigener dynamischer Zweig wurde am 2026-07-22 "zum Test entfernt" (`c47e4993`).    |

Das Deck sagt es in einem Satz: "Ein Pfad, den ein Paket gestern genommen hat, ist keine
Zusage, dass er morgen noch existiert."

### 3.2 Die Gegenrichtung ist eine andere Messung

Der Vorschlag kehrt einen gehörten Pfad um ("Known path D-C-B-A, transmission from A:
B,C,D"). Ein HEY-Pfad hält fest, wer wen gehört hat, in einer Richtung, mit dem RSSI des
Empfängers. Live-Daten der Karte für den Freisinger Cluster, 7-Tage-Fenster, beide Richtungen
desselben physischen Paars:

| Richtung 1 (Empfänger zuerst)                       | Richtung 2 (Empfänger zuerst)                      | Delta RSSI / SNR |
| --------------------------------------------------- | -------------------------------------------------- | ---------------- |
| DL2JA-2 hört DC2MAC-1: -110,0 dBm / -7,6 dB (282)   | DC2MAC-1 hört DL2JA-2: -121,6 dBm / -15,3 dB (8)   | 11,6 / 7,7 dB    |
| DL2JA-2 hört DL3NCU-1: -115,3 dBm / -11,9 dB (146)  | DL3NCU-1 hört DL2JA-2: -115,3 dBm / -4,7 dB (3)    | 0,0 / 7,2 dB     |
| DL2JA-2 hört DB0HOB-12: -119,6 dBm / -14,6 dB (135) | DB0HOB-12 hört DL2JA-2: -124,3 dBm / -9,2 dB (497) | 4,7 / 5,4 dB     |
| DL2JA-2 hört DB0ED-99: -97,8 dBm / +1,6 dB (334)    | DB0ED-99 hört DL2JA-2: -91,6 dBm / +5,3 dB (69)    | 6,2 / 3,7 dB     |
| DK5EN-98 hört DB0ED-99: -120,0 dBm / -9,0 dB (268)  | DB0ED-99 hört DK5EN-98: -115,3 dBm / -6,4 dB (67)  | 4,7 / 2,6 dB     |

Die Stichprobenzahlen sind auch vom Feed-Problem in Abschnitt 7 geprägt, die Evidenz sind die
dB-Spalten. Bei SF11 beträgt die Dekodierreserve wenige dB; eine Asymmetrie von 5 bis 12 dB
ist der Unterschied zwischen einem Link, der geht, und einem, der nicht geht. Der
DG0OPK-Standort liefert den Grund: der Rauschteppich dort liegt bei -86 bis -94 dBm gegen
theoretisch -114 dBm, und er ist standortspezifisch, der Knoten am ruhigen Standort hört also
weit mehr, als er gehört wird.

### 3.3 Der heutige VIA-Mechanismus überlebt nur einen Hop

`checkMesh()` (`src/via_functions.cpp:50-93`) lässt einen Knoten einen VIA-Frame nur
weiterleiten, wenn sein eigenes Rufzeichen im Zielpfad steht. Das Relay selbst schreibt den
Frame aber um (`src/lora_functions.cpp:1486-1488`, upstream `:1460-1462`):

```cpp
aprsmsg.msg_destination_path = aprsmsg.msg_destination_call;
checkVia(aprsmsg);   // setzt einen Pfad nur aus der statischen --setvia-Einstellung
```

`A>B,C,D:` verlässt B also als `A,B>D:` ohne VIA-Liste. Ab dem zweiten Hop flutet jeder Knoten
wie gewohnt. Das Drei-Hop-Beispiel des Vorschlags ist damit mit keiner Firmware im Feld
möglich; jedes Relay müsste die Restliste bewahren und abarbeiten. Das ist eine
Verhaltensänderung im Relay-Pfad aller 1.449 meldenden Knoten (Flotte heute: 304 auf 4.35t,
122 auf 4.35s, 762 auf 4.35p, 261 älter), und der Übergang in der gemischten Flotte ist der
riskante Teil, nicht der Absender.

Zwei kleinere Defekte im bestehenden Gatter: die Mitgliedsprüfung ist ein Substring-Vergleich
(`indexOf(node_call)`, ein Pfad mit `OE1KBC-71` triggert also auch `OE1KBC-7`), und die Liste
ist ungeordnet, jeder benannte Knoten, der den Frame hört, leitet ihn weiter, ob er der nächste
Hop ist oder nicht.

### 3.4 Eine Kette multipliziert Verluste und summiert Latenz; eine Flut nimmt das Minimum

Der Relay-Erfolg je Hop im ruhigen DG0OPK-Cluster liegt bei 99,2 bis 99,7 %, gegeben Empfang.
Über die Zustellung entscheidet nicht diese Zahl, sondern die Warteschlange vor jedem Relay:

| Gemessen an DG0OPK-11 (belasteter Standort, 47,6 h) | Text-Relay | Positions-Relay |
| --------------------------------------------------- | ---------- | --------------- |
| RX bis eigenes Relay auf Sendung, Median            | 30,0 s     | 182 s           |
| p99                                                 | 286 s      |                 |
| max                                                 | 985 s      | 2197 s          |
| gewonnene CSMA-Fenster                              | 21,4 %     |                 |

Die Geschwister am selben Standort waren 2- bis 4-mal schneller. In einer Flut nimmt die DM
dasjenige der 2 bis 4 Relais, die sie gehört haben, das zuerst auf Sendung kommt; die
Warteschlange des langsamen Knotens ist unsichtbar. In einer Source-Route über B, C, D wartet
die DM in Bs Warteschlange, dann in Cs, dann in Ds, ohne Alternative, und ein belegtes Relay
ist die Zustellzeit. Bei Verlust p je Hop liefert eine Kette aus 3 Relais mit (1-p)^3; die
Flut liefert, wenn irgendeines der parallelen Relais durchkommt. Die Redundanz der Flut ist
genau die Eigenschaft, die der Vorschlag ausgibt.

Die Quittung profitiert auch nicht: `:ackNNN` ist ein normaler Text-Frame und flutet durch das
Mesh zurück (`docs/ack-wer-hat-quittiert.md`). Die Ersparnis liegt nur auf dem Hinweg, der
Hälfte des Umlaufs.

### 3.5 Die Fehlerleiter erbt die zwei Defekte, die wir gerade beheben

- **Retry mit derselben msg_id.** "One AUTO-VIA retry" mit der ursprünglichen msg_id wird von
  jedem Knoten verworfen, dessen Dedup-Ring die Kennung hält, also von jedem, der den ersten
  Versuch gehört hat, einschließlich des Ziels (`is_new_packet()` in
  `src/lora_functions.cpp:679`; Ringfenster 28,8 bis 52,3 min, `src/dedup_functions.h`). Der
  Retry hilft nur, wenn der erste Hop die DM nie gehört hat. Das ist Defekt 1 des
  DM-Transportvorschlags, und deshalb bekommt dort jede erneute Flut eine frische msg_id und
  eine stabile Sequenznummer (`{NNN`).
- **Timer gegen Umlaufzeit.** Der bestehende Retry feuert nach etwa 40 s (`MAX_RETRANSMIT`
  3), der gemessene Umlauf beträgt etwa 1 min im ruhigen und 4 min Median im belasteten Netz.
  Eine Route, die "vorübergehend gesperrt" wird, weil die Quittung nicht in 40 s kam, ist aus
  einem Timing-Grund gesperrt, nicht aus einem Routing-Grund. Der Rückfall flutet dann mit
  neuer msg_id, der Normalfall im belasteten Netz ist also VIA plus Flut: mehr Airtime als die
  Flut allein.
- **Routenzustand auf dem klassischen ESP32.** Gesperrte Routen, Retry-Zähler und Alternativen
  brauchen RAM auf Boards mit etwa 6,6 kB DRAM-Reserve nach den geplanten Fixes
  (`docs/mem-headroom-classic-esp32-20260905.md`).

### 3.6 Der Gewinn ist klein und das Risiko sitzt auf dem sichtbaren Verkehr

Mengen vom Kartenserver, 7 Tage bis 2026-09-09: 1.182 Direktnachrichten von 185 Absendern,
gegen 2.601 Gruppennachrichten und 3.600 bis 4.600 HEY-Baken plus etwa 3.800 Positionen je
Stunde netzweit. DMs sind unter einem Prozent der Frames. Selbst ein perfektes DM-Routing
ändert die Netz-Airtime um eine Zahl, die niemand messen kann, während die DM der eine Frame
ist, dessen Verlust ein Nutzer sieht. Die VIA-Liste selbst kostet etwa 20 Byte je benanntem
Hop auf dem Draht (0,24 s bei SF11/BW250 je Sendung), eine Drei-Hop-Liste also etwa eine
halbe Sekunde je Kopie.

### 3.7 Zusammenspiel mit Gateways und Server

Der Gateway-Upload passiert vor der Mesh-Entscheidung (`src/lora_functions.cpp:1390-1415`),
eine VIA-geroutete DM wird also weiterhin von jedem Gateway hochgeladen, das sie hört, und der
Serverweg funktioniert weiter. Ein Gateway, das nicht in der Liste steht, leitet aber nicht
weiter, die internetgebrückte Hälfte des Netzes verliert also das Funk-Relay genau der Knoten,
die meist die besten Standorte haben.

---

## 4. Was wir stattdessen tun würden

### 4.1 Zuerst Zuverlässigkeit: die Quittung mit zwei Kennungen (gebaut, noch nicht am Bench)

`docs/proposal-dm-transport-reliability-20260909.md` und der Kampagnenstand in
`docs/dm-transport-impl-plan-20260913.md`: Stufe 0 (echo-gesteuerter Retry mit gleicher
Kennung, erneute Quittung bei Duplikat-für-mich, Fehlermeldung an die App), Stufe 1 (Outbox
und RTT-beabstandete Leiter hinter `--dmretry`, frische msg_id je erneuter Flut, stabiles
`{NNN`), Stufe 2.1 (Ziel-Dedup auf Quelle plus NNN), Stufen 3 und 4 (Speicherknoten,
Verwahrungsmeldung). Nichts davon braucht einen neuen Frame-Typ; Altknoten sehen byteidentische
Frames. Das ist es, was das "ACK failure handling" des Vorschlags darunter braucht, egal wie
der Hinweg aussieht.

### 4.2 Dann Airtime: weiche Relay-Bevorzugung statt harter Routen

Die Flut bleibt, es ändert sich, wer zuerst geht. Zwei Eingänge gibt es auf jedem Knoten
schon: den gelernten PATH (welche Relais die Bake dieses Ziels getragen haben) und die von den
Nachbarn gemeldeten Nachbarzahlen (`R<NC>;`, ADR-02 `docs/adr-nc-importance-backoff.md`). Ein
Relay, das im gelernten Pfad zum Ziel der DM steht oder eine hohe Netzwichtigkeit hat, nimmt
einen frühen CSMA-Slot; alle anderen nehmen einen späten und verwerfen ihre wartende Kopie,
wenn sie vorher eine hören (der Schritt "eingereihtes Relay stornieren, wenn eine Kopie gehört
wird", analysiert in `docs/prio-talk-flood-networking.md`). Ergebnis: im guten Fall läuft die
DM den gelernten Pfad mit einer Sendung je Hop-Ebene, genau die Ersparnis des Vorschlags; im
schlechten Fall tragen die anderen Relais sie trotzdem. Keine Wire-Änderung, kein
Routenzustand, keine Fehlerleiter, Altknoten fluten einfach weiter wie heute. Das ist der
Ameisenpfad aus dem Deck: lokale Regeln, globale Ordnung.

### 4.3 Vorher messen

Das Netz hat heute keine Messung des DM-Ausgangs. Stufe 0 ergänzt DM-Zähler in der
setlog-STAT-Zeile (gesendet, Echo gehört, Gateway-ACK, Peer-ACK, aufgegeben, Zeit bis ACK). Ein
PoC, der kein Vorher/Nachher auf diesen Zahlen zeigen kann, lässt sich nicht beurteilen, und
das würden wir in der Discussion auch so sagen.

---

## 5. Antworten auf die vier Fragen, wie wir sie geben würden

1. **Ist automatisches VIA-Routing schon geplant?** Nicht im Fork. Der dynamische Zweig in
   `checkVia()` war Kurts, im Juli probiert und entfernt; die Upstream-Antwort ist seine.
2. **Ist die PATH-Tabelle für Routing-Entscheidungen gedacht?** Sie ist eine diagnostische Spur
   (Abschnitt 3.1). Sie kann eine Relay-Bevorzugung speisen; eine Route kann sie nicht tragen.
3. **Passt der Ansatz zur Richtung von MeshCom?** Das Ziel ja (zuverlässige, billige DMs).
   Hartes Source-Routing passt nicht zu einem Flutnetz im geteilten ISM-Band; eine weiche
   Bevorzugung auf der Flut schon.
4. **Wäre ein PoC/PR willkommen?** Ein PoC, der zuerst belegt, dass der Relay-Pfad eine
   VIA-Liste über eine gemischte Flotte bewahren kann, und der den DM-Ausgang vorher und nachher
   misst, wäre lesenswert. Ein PR, der dem Absender Routenzustand und einen Retry mit gleicher
   msg_id hinzufügt, nicht.

---

## 6. Kurts "msg_id behalten, Sequenznummer ergänzen": die eine Frage, die zählt

Der Draht trägt je DM schon zwei Kennungen: die 32-Bit-msg_id (22 Bit Knoten-ID, 10 Bit
Zähler, `docs/architecture/11-wire-format.md` Abschnitt 1.1) und den dreistelligen
`{NNN`-Zähler in der Nutzlast. Heute sind das dieselben zehn Bit. Wie das neue Schema auch
heißt, eine Frage entscheidet, ob es in der bestehenden Flotte funktioniert:

| Schema                                                          | Altknoten, der Versuch 1 gehört hat     | Ziel, das Versuch 1 gehört hat, dessen ACK aber verloren ging   | Server / Karte                                 |
| --------------------------------------------------------------- | --------------------------------------- | --------------------------------------------------------------- | ---------------------------------------------- |
| Retry behält die msg_id, Sequenznummer ändert sich              | verwirft den Retry (Dedup-Ring)         | verwirft den Retry, quittiert nie erneut                        | sieht eine Nachricht                           |
| Retry bekommt frische msg_id, Sequenznummer bleibt (unser P2)   | leitet den Retry als neuen Frame weiter | quittiert erneut, zeigt nichts doppelt (Dedup auf Quelle + NNN) | sieht neue msg_id, muss auf NNN zusammenfalten |
| Retry behält die msg_id, Dedup-Schlüssel wird (msg_id, Sequenz) | verwirft den Retry bis zum Update       | dito bis zum Update                                             | sieht eine Nachricht                           |

Nur die zweite Zeile funktioniert, bevor die ganze Flotte aktualisiert ist, und das
"zusätzliche sequenzielle Nummerierungssystem", das sie braucht, existiert schon als `{NNN`.
Meint Kurt die dritte Zeile, ist der Übergang dieselbe flottenweite Relay-Änderung wie in
Abschnitt 3.3, und der Nutzen kommt erst, wenn der letzte 4.35p-Knoten weg ist. Die Frage an ihn
lautet deshalb nicht "neue msg_id oder nicht", sondern "welche der beiden Kennungen ist bei
einer Wiederholung frisch".

---

## 7. Die Meshmap-Frage zu den Gateway-Links

### 7.1 Die Hypothese, geprüft

"Die Antwort auf eine HEY-Path-Anfrage hat dieselbe msg_id und läuft in den Dedup-Ring." Im
HEY-Mechanismus gibt es kein Anfrage/Antwort-Paar; ein HEY ist eine Bake, und die Empfänger
hängen ihren Report an die Kopie, die sie hochladen oder weiterleiten. Die Idee "gleiche
msg_id" selbst wurde zweimal gemessen:

| Datum      | Wo die Kopie stirbt                                                                                                                                  | Ergebnis                                                                                                                                                                                                                                          |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-08-18 | Dedup-Ring des Knotens (Server schiebt das HEY zuerst zum Gateway, die HF-Kopie ist ein Duplikat)                                                    | **Widerlegt.** Der Server schob in 105 min 0 HEYs zu DK5EN-98 (86 Pushes, alle Text). 9 von 9 direkt gehörten Gateway-HEYs waren `RX_DEDUP_NEW` und wurden hochgeladen (`TX-UDP`). mcmap `docs/findings/data-gateway-link-blindspot.md` 9.7, 10.1 |
| 2026-08-18 | Server behält einen Report je msg_id, der nackte Selbstupload des Gateways kommt 3 bis 8 s früher an                                                 | Damals beobachtet: 0 von 9 veröffentlicht; eine 707 s späte Kopie wurde veröffentlicht.                                                                                                                                                           |
| 2026-08-31 | Dito, Zwei-Knoten-Bench DK5EN-93 / DK5EN-92 mit dem Interlink-Log als Referenz                                                                       | Der Server verteilte **jede** Kopie (nackter Selbstupload plus zwei angereicherte). Der Verlust lag beim Konsumenten, der den ersten Datensatz je msg_id behält. Fix GW-01: kein Selbstupload des eigenen HEY. `docs/BACKLOG.md` 3.8i             |
| 2026-09-16 | Feed heute: DL2JA-1-Frame `E9F1108C` erscheint mit vier verschiedenen Empfänger-Reports in derselben Sekunde, dann relayte Kopien 13 bis 22 s später | Kein Dedup je msg_id im heutigen Feed. GW-01 ist in Upstream 4.35t; DK5EN-98s eigene HEYs zeigen 0 nackte Selbstuploads und 11 Frames mit Empfängern in den ersten 50 MiB des aktuellen Logs.                                                     |

Also: der Ring ist es nicht, das Selbstupload-Rennen war real und ist für 4.35t-Knoten
behoben, und die Karte zeigt trotzdem keinen Link DL2JA-2 zu DK5EN-98 (0 Samples in 7 Tagen, 0
Frames `"path":"DL2JA-2,DK5EN-98"` im gesamten aktuellen Log), obwohl DK5EN-98 DL2JA-2 alle
paar Minuten mit etwa -111 dBm hört. Etwas anderes ist jetzt bindend.

### 7.2 Was der Feed heute zeigt: der Empfänger ist anonym

Kontrollpaar aus dem August-Befund, OE5HWN-14 gehört vom Gateway OE5HWN-12, beide inzwischen
auf 4.35t:

```
2026-08-18   {"type":"hey","path":"OE5HWN-14,OE5HWN-12","rssi":"61,6", ...}
2026-09-16   {"type":"hey","path":"OE5HWN-14","rssi":"2,50,6;", ...}     (alle 15 min, sechs geprüft)
```

Derselbe physische Empfang. Im August nannte der Pfad das empfangende Gateway, und der Hub
hängte dessen RSSI/SNR aus dem UDP-Header an. Heute trägt der Frame den Report des Empfängers
als firmware-eigene Dreiergruppe (Nachbarzahl, RSSI, SNR) und kein Rufzeichen. Zählungen in den
ersten 50 MiB des laufenden Interlink-Logs (622.690 Zeilen):

| Frame-Form                                                               | Zahl heute | Zahl 2026-08-26                     |
| ------------------------------------------------------------------------ | ---------- | ----------------------------------- |
| Pfad mit einem Element, Report vorhanden, Empfänger unbenannt (netzweit) | 9.028      | 209                                 |
| Dieselbe Form, nur Origin DL2JA-1                                        | 90         | 27 (08-30), 83 (09-13)              |
| Pfad mit einem Element, kein Report (nackt)                              | 34.718     |                                     |
| Zwei oder mehr Pfadelemente (relayte Kopien)                             | 27.485     |                                     |
| Letztes Segment ein vom Hub angehängtes Zweierpaar (Empfänger benannt)   | **0**      | **0**                               |
| Letztes Segment eine Firmware-Dreiergruppe                               | 29.567     |                                     |
| `"path":"DL2JA-1,DK5EN-98"` (DK5EN-98 als Empfänger benannt)             | 0          | 0 (auch 08-28, 08-30, 09-06, 09-13) |

Das Wachstum der ersten Zeile von 209 auf 9.028 folgt dem 4.35t-Rollout: nur Knoten mit dem
Report in der Nutzlast (Fork seit 2026-08-21, Upstream seit 4.35t) erzeugen einen
Ein-Element-Frame mit Werten; der Direktempfang eines 4.35p-Gateways kommt nackt an, ohne Werte
und ohne Namen.

Seit einem Tag zwischen 2026-08-18 und 2026-08-26 nennt der INTERLINK-Feed das hochladende
Gateway bei keinem Upload mehr, alte Firmware oder neue. Ein Direktempfang ist jetzt ein
Ein-Element-Pfad mit Report, und mcmap bildet eine Kante nur, wenn der Pfad zwei Elemente hat
(`proxy/src/interlink/ingest.ts:1209`, `pathNodes.length >= 2`); der Ein-Element-Fall wird
dort als "etwa 215 geflattete Pfade am Tag" behandelt, was auf dem alten Feed stimmte und jetzt
etwa 9.000 je 50 MiB sind. Der Report ist da, der Empfänger nicht, also kann keine Karte den
Link zeichnen.

Deshalb trifft es Gateway-zu-Gateway-Paare am härtesten: ein Gateway erscheint nur dann in
einem Pfad, wenn es relayt, und ein Gateway mit Mesh aus (DK5EN-98) oder eines, dessen Relays
das CSMA-Rennen verlieren, erscheint gar nicht. DK5EN-98s Report über DL2JA-1 steht im Feed als
`"path":"DL2JA-1","rssi":"7,118,-12;"` (7 Nachbarn, -118 dBm, passt exakt zu DK5EN-98), und er
ist nicht zuordenbar.

Zwei Nebeneffekte, die man kennen sollte:

- Ein nackter Ein-Element-Frame ist jetzt mehrdeutig: ein Selbstupload von Firmware vor GW-01,
  oder ein Upload von einem der 762 4.35p-Gateways, die nichts an die Nutzlast anhängen. Die
  94 nackten DL2JA-2-Frames am Tag (35 plus 59 im aktuellen Log) sind sehr wahrscheinlich
  Letzteres; eine Konsolenzeile auf DL2JA-2 würde es klären. mcmaps
  `selfReportShape`-Heuristik (`ingest.ts:1162`) markiert den Origin bei dieser Form als
  Gateway und greift jetzt falsch (3 nackte Frames heute für DL2JA-1, das kein Gateway ist).
- Die Regel "ein Report je msg_id" vom 2026-08-18 ist im heutigen Feed nicht sichtbar; entweder
  hat sich der Hub im selben Fenster geändert, oder die Regel saß in der WSS-Pipeline, die mcmap
  seit Mitte August nicht mehr empfängt (heydata-Archive enden 2026-08-14).

### 7.3 Bewertung

Deine Hypothese in der Knoten-Ring-Form ist durch zwei Messungen widerlegt. In der Server-Form
war sie richtig, bis GW-01 in 4.35t kam. Die bindende Ursache heute ist, dass das meldende
Gateway seit Ende August bei keinem Upload mehr im Feed benannt wird. Ob das eine
Serveränderung ist (der Hub hängt Rufzeichen und Header-RSSI/SNR nicht mehr an) oder eine
Serverregel, die das neue Report-Format in der Nutzlast auslöst, kann nur Kurt beantworten, und
für ihn ist das billig.

Fix-Kandidaten, in dieser Reihenfolge:

1. **Hub:** das Rufzeichen des meldenden Gateways wieder anhängen (wie vor 2026-08-18), ob die
   Nutzlast die Werte schon trägt oder nicht. Keine Firmware-Änderung, ganze Flotte auf einmal.
2. **Firmware:** im Upload-Zweig (`src/lora_functions.cpp:1390-1400`) das eigene Rufzeichen
   neben dem Report an den Quellpfad hängen, spiegelbildlich zum Relay-Zweig. Eine Zeile, ein
   Upstream-PR, aber vorher mit dem Hub-Betreiber abstimmen, damit das Rufzeichen nicht doppelt
   steht, sobald der Hub es wieder tut.
3. **mcmap allein kann es nicht beheben**; der Empfänger steht nicht in den Daten. Der
   knotenlokale Abgriff (Option E3 des Befunds) bleibt der Umweg für unsere eigenen Knoten.

---

## 8. Fragen an dich

1. **Jetzt posten oder nach dem Gespräch?** Abschnitt 1.1 ist für den Discussion-Thread
   geschrieben. Kurt hat dort schon geantwortet; unsere Position vor eurem Gespräch zu posten
   könnte ihm vorgreifen.
2. **Welche DM-Stufe geht zuerst upstream?** Stufe 0 war der PR-große Kandidat. Willst du sie in
   der Discussion als konkrete Alternative anbieten oder für das Gespräch aufheben?
3. **Weiche Relay-Bevorzugung (4.2): zuerst im Fork oder nur als Vorschlag?** Sie braucht die
   Zähler je Nachbar, die das Deck als fehlenden Schritt aus Stufe 0 nennt. Nichts ist gebaut.
4. **Der Karten-Fix (7.3): wem gehört er?** Kurt nach der Hub-Änderung fragen, oder den
   Einzeiler-Firmware-PR schicken und ein mögliches doppeltes Rufzeichen später in Kauf nehmen?
   Meine Empfehlung: zuerst fragen.
5. **Nackte DL2JA-2-Frames:** bekommst du eine Konsolenzeile von Werner (DL2JA-2, 4.35t), die
   bestätigt, dass er nicht mehr selbst hochlädt? Das entscheidet, ob die 94 nackten Frames am
   Tag alte Gateways sind, die ihn hören.
6. **Sollen das Kontrollpaar OE5HWN-14/-12 und die heutigen Zählungen in den mcmap-Befund
   zurückgeschrieben werden**, damit das August-Dokument nicht mehr den Ring oder die msg_id
   als Ursache nennt?

---

## 9. Quellen

- Text der Discussion #1146 und OE1KBCs Antwort (GitHub GraphQL, 2026-09-16 16:00 UTC).
- Firmware: `src/via_functions.cpp`, `src/lora_functions.cpp` (RX-Gatter :679, Upload
  :1390-1415, Relay-Umschreibung :1486-1488), `src/mheard_functions.cpp` (`updateHeyPath()`
  :460-640), `src/loop_functions.cpp` (`sendHey()` :4949), `src/dedup_functions.h`,
  `src/configuration_global.h` (Ringgrößen, Trickle, `HEY_PATH_PAYLOAD_MAX`); Upstream
  `4058b25b` für dieselben Stellen; `git log upstream/dev -- src/via_functions.cpp`.
- Konzeptpapiere: `docs/presentation/meshcom-protocol.html` (veröffentlicht unter
  https://dk5en.github.io/MeshCom-Firmware/), `docs/proposal-dm-transport-reliability-20260909.md`,
  `docs/concept-dm-store-and-forward.md`, `docs/adr-nc-importance-backoff.md`,
  `docs/prio-talk-flood-networking.md`, `docs/hey-supp.md`, `docs/hey-storm-analysis-20260827.md`,
  `docs/dm-transport-impl-plan-20260913.md`, `docs/BACKLOG.md` 3.8i (GW-01).
- Felddaten: DG0OPK-Kampagnen (Memory `dg0opk-mesh-findings`, `dedup-ring-size-settled`);
  BergLog 2026-03 in ADR-02.
- Kartenserver (mcmap-prod MCP, 2026-09-16 15:40 bis 16:30 UTC): `link_quality_overview` für
  DK5EN-98 und DL2JA-2 (7 d), `nodes_query`, `fleet_firmware`, `logs_grep` auf `interlink`
  (laufende Datei 92 MB, Archive 2026-08-26/28/30, 09-06, 09-13), Ressourcen
  `docs://hey-format` und `docs://feature-link-quality`; mcmap-Repo
  `docs/findings/data-gateway-link-blindspot.md`,
  `docs/findings/gw-hey-rx-report-baseline-20260818.md`, `proxy/src/interlink/ingest.ts`,
  `proxy/src/interlink/heyDedupe.ts`, `proxy/src/live/frameDedup.ts`.
