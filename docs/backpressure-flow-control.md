# Rueckstau-Meldungen (BP-01 / Q-Codes) — Referenz fuer Nutzende und App-Entwicklung

Stand: 2026-09-01, Firmware v4.35p.08.31.4-stability
Quellen im Code: `src/backpressure.h`, `src/bp_notice_frame.h`, `src/extern_notice_json.h`,
`src/loop_functions.cpp` (Abschnitt "BP-01 ... back-pressure to the sender")
Geplante Aenderung: `docs/bp-l1-l4-impl-plan.md` (BP-07/08/09, noch nicht implementiert)

Dieses Dokument beschreibt, was ein MeshCom-Node meldet, wenn sein Sendepuffer volllaeuft,
in welcher Form diese Meldungen auf den einzelnen Transportwegen ankommen, und was eine
Chat-App daraus an Flusssteuerung bauen kann. Es ist bewusst dreiteilig: Kapitel 1-4 fuer
Nutzende, Kapitel 3-7 fuer App-Entwicklung, Kapitel 8-11 fuer Firmware- und App-Entscheidungen.

Kapitel 1-8 beschreiben den **ausgelieferten Stand**. Kapitel 9 beschreibt eine **geplante,
noch nicht implementierte** Aenderung und ist als solche gekennzeichnet.

---

## Kurzfassung

Ein Node haelt seine ausgehenden Frames in einem Ringpuffer (`MAX_RING`, meist 20 Plaetze).
LoRa ist langsam — ein Textframe belegt je nach Modulation ein bis mehrere Sekunden Sendezeit.
Wer schneller tippt, als der Funk abfliesst, fuellt diesen Ring. Frueher verschwanden solche
Nachrichten wortlos. Seit BP-01 meldet der Node den Fuellstand in vier Q-Codes zurueck — und
zwar **immer nur auf dem Weg, ueber den die Nachricht hereinkam, niemals ueber die Luft**.
Eine Rueckstaumeldung, die gefunkt wird, vergroessert genau den Stau, den sie meldet.

| Code    | Bedeutung                               | Nimmt der Node noch an? |
| ------- | --------------------------------------- | ----------------------- |
| **QRS** | Sende langsamer, der Puffer fuellt sich | ja                      |
| **QRT** | Ich nehme nichts Neues mehr an          | nein                    |
| **QTA** | Deine Nachricht wurde verworfen         | nein                    |
| **QRV** | Wieder bereit, Puffer ist leer          | ja                      |

---

## 1. Die vier Q-Codes im Detail

Die Logik sitzt in `src/backpressure.h` und ist bewusst frei von Arduino-Abhaengigkeiten,
damit sie in `test/test_backpressure` (56 Faelle) auf dem Host festgenagelt werden kann.

| Code    | Ausloeser                                                     | Text auf dem Draht                                      |
| ------- | ------------------------------------------------------------- | ------------------------------------------------------- |
| **QRS** | Ringtiefe erreicht 5 (`QRS_MIN_DEPTH`, fest auf allen Boards) | `QRS - slow down, TX buffer is filling`                 |
| **QRT** | Ringtiefe erreicht 80 % von `MAX_RING`                        | `QRT - stopping to accept new messages, TX buffer full` |
| **QTA** | Der Ring hat einen Frame tatsaechlich verworfen               | `QTA - message discarded, TX buffer full`               |
| **QRV** | Tiefe 0, oder Tiefe 1 fuer 10 s ununterbrochen                | `QRV - ready again, TX buffer clear`                    |

Warum QRS erst bei 5: eine 5,5-Minuten-Messung auf dem Gateway DK5EN-98 am 2026-08-31 zeigte
im Normalbetrieb ohne jeden Burst eine Grundlast von Tiefe 1-4 (Modus 2). Die alte Regel
(Tiefe > 1) sass direkt auf dieser Grundlast und erzeugte drei falsche QRS/QRV-Paare in
5,5 Minuten. Die Schwelle ist bewusst ein fester Wert und kein Prozentsatz von `MAX_RING` —
Vorhersagbarkeit vor Skalierung.

### Schwellen pro Board

| Board-Klasse                                     | `MAX_RING` | QRS ab | QRT ab |
| ------------------------------------------------ | ---------- | ------ | ------ |
| ESP32-S3 (Heltec V3, T-Deck), nRF52840 (RAK4631) | 20         | 5      | 16     |
| ESP32 classic (T-Beam, E22)                      | 20         | 5      | 16     |
| `ENABLE_TBEAM` Entwickler-Build                  | 10         | 5      | 8      |

### Episodenmodell

Der Automat kennt drei Zustaende und einen Latch, der innerhalb einer Episode nur aufwaerts
laeuft: `keiner -> QRS -> QRT -> QTA`. Deshalb bekommt ein Nutzer bei einem Burst von zwanzig
Nachrichten **eine** QRS und **eine** QRT, nicht zwanzig. Eine Meldung pro Zustandswechsel,
nicht pro Nachricht.

```
                  Tiefe >= 5              Tiefe >= 80 %          Ring verwirft
   [ QUIET ] ---------------> [ QRS ] ---------------> [ QRT ] --------------> [ QRT ]
      ^         nimmt an        |        nimmt an        |     weist ab   Latch QTA
      |                         |                        |
      |     Tiefe 0 sofort, oder Tiefe 1 fuer 10 s       |
      +---------------------------------------------------+
                    QRV  (nur wenn der Latch QRT oder QTA erreicht hatte)
```

QRV ist die schliessende Klammer einer Warnung, kein Herzschlag: eine Episode, die nur QRS
erreicht hat und dann von allein abgeflossen ist, schliesst **stumm**. Wer nur "langsamer"
gehoert hat und dann sieht, dass alles laeuft, braucht keine zweite Meldung. Wer eine
Nachricht verloren hat, braucht die Entwarnung am dringendsten.

---

## 2. Was abgewiesen wird und was nicht

Nur **lokal erzeugte Nutzernachrichten** werden abgewiesen — also das, was jemand in der App,
im Webinterface, auf der Konsole oder am T-Deck tippt. Relaisverkehr, ACKs und Bakenframes
laufen nicht durch `sendMessage()` und werden nie abgewiesen. Ein verstopfter Node bleibt
damit ein funktionierendes Relais, waehrend der flutende Nutzer gebremst wird.

Technisch: jeder Aufrufer markiert die Herkunft direkt vor `sendMessage()` und raeumt sie
danach wieder ab. Frames ohne Herkunftsmarke sind grundsaetzlich unabweisbar.

| Herkunft        | Gesetzt in                                       |
| --------------- | ------------------------------------------------ |
| `ORIGIN_SERIAL` | `esp32_main.cpp:4286`, `nrf52_main.cpp:2931`     |
| `ORIGIN_BLE`    | `esp32_main.cpp:2971`, `nrf52_main.cpp:1616`     |
| `ORIGIN_WEB`    | `web_functions.cpp:2192`                         |
| `ORIGIN_EXTUDP` | `extudp_functions.cpp:334`                       |
| `ORIGIN_GUI`    | `event_functions.cpp:716`, `ui_deckpro.cpp:1804` |

Die Abweisung passiert **vor** allen Seiteneffekten: keine Message-ID wird verbraucht, nichts
wird ins Flash geschrieben, nichts landet im Eigen-TX- oder Dedup-Speicher. Eine abgewiesene
Nachricht hinterlaesst im Node keine Spur ausser der Logzeile.

---

## 3. Das Echo als Empfangsquittung

Auf BLE, Web und EXTUDP schickt der Node jede angenommene Nachricht als Frame an den Absender
zurueck. **Das ist die einzige Bestaetigung, dass der Text am Node angekommen und akzeptiert
worden ist** — ein zweites Signal dafuer gibt es nicht. Jede Flusssteuerung in einer App haengt
daran, deshalb steht es vor den Drahtformaten.

| Weg              | Echo                                                                                     |
| ---------------- | ---------------------------------------------------------------------------------------- |
| BLE              | vollstaendiges Frame in den Telefonpuffer, identisch zu dem, was gesendet wird           |
| Web-GUI          | derselbe Puffer                                                                          |
| EXTUDP           | JSON-Datagramm `"src_type":"lora"`, `"type":"msg"`                                       |
| T-Deck GUI       | Eintrag in der Nachrichtenliste auf dem Bildschirm                                       |
| Serielle Konsole | **kein Echo** — nur die Debugzeile `NEW-TXT`, und die nur bei eingeschaltetem Info-Debug |

Eine ueber die Konsole mit `::` getippte Nachricht wird an BLE und Web gespiegelt, nicht auf die
Konsole selbst.

### Was das Echo aussagt und was nicht

Das Echo traegt die **echte `msg_id`** der Nachricht. Sie ist der Schluessel fuer alles, was
spaeter zu dieser Nachricht kommt — insbesondere die `0x41`-Statusframes:

| Statusbyte | Bedeutung                                      |
| ---------- | ---------------------------------------------- |
| `0x01`     | Server erreicht (nur auf einem Gateway mit IP) |
| `0x02`     | ACK vom Empfaenger (nur bei DM)                |

Bei einer DM haengt am Nutztext zusaetzlich `{NNN}` — die dreistellige ACK-Anforderung. Der
Echo-Text ist dadurch nicht byteidentisch mit dem, was der Nutzer getippt hat.

Wichtig fuer die Auslegung:

- Das Echo bedeutet **nicht**, dass die Nachricht auf der Luft war. Dafuer sind die
  `0x41`-Statusframes da.
- Im heutigen Stand bedeutet es genau genommen nur "der Node hat den Text geparst und
  angenommen" — es geht raus, **bevor** der Sendepuffer ueberhaupt gefragt wurde (Luecke L3,
  Kapitel 8). Nach BP-08 bedeutet es "die Nachricht liegt im Sendepuffer".
- **Bleibt das Echo aus, ist die Nachricht nicht angenommen worden.** Das ist heute der
  einzige zuverlaessige Weg, eine abgewiesene Nachricht zu erkennen — siehe Kapitel 11.

---

## 4. Wo die Meldung ankommt

| Herkunft   | Zustellweg                                               |
| ---------- | -------------------------------------------------------- |
| Konsole    | Klartext auf der seriellen Schnittstelle                 |
| BLE        | APRS-Frame in den Telefonpuffer, siehe Kapitel 5         |
| Web-GUI    | derselbe Puffer, das Webinterface liest ihn mit          |
| EXTUDP     | JSON-Datagramm an den externen Peer, siehe Kapitel 6     |
| T-Deck GUI | Klartextzeile in der Nachrichtenliste auf dem Bildschirm |

Die Meldung geht an **dasselbe Ziel wie die ausloesende Nachricht** (BP-06). Wer in Gruppe 232
getippt hat, sieht die Meldung im Chat 232 und nicht in einem Broadcast-Fenster. Bei einer DM
erscheint die Meldung im eigenen DM-Verlauf mit diesem Partner — der Partner selbst sieht sie
nie, das Frame verlaesst den Node nicht.

Faellt der Transportweg zwischendurch weg (BLE getrennt, Webserver aus), wird die Meldung
verworfen statt gepuffert. Ein QRV, das beim naechsten Verbinden eintrudelt, ist Rauschen.

---

## 5. Drahtformat BLE / Web

Die Meldung ist ein ganz normales Textnachrichten-Frame, abgesetzt unter dem **eigenen
Rufzeichen des Nodes**. Das ist Absicht: ein Pseudo-Absender wie `response` ist kein gueltiges
Rufzeichen und landet in McApp in der Spam-Klasse (Gruppe 9999), wo die Meldung niemand sieht.
Kommandoantworten benutzen weiterhin `response`, Rueckstaumeldungen nicht.

```
Byte  0      0x3A                          payload_type ':' (Textnachricht)
Byte  1..4   msg_id, little endian         = millis() bei einer Meldung
Byte  5      Flags | max_hop
               Bit 0x80  msg_server
               Bit 0x40  msg_track
               Bit 0x20  msg_app_offline   << bei jeder Meldung gesetzt
               Bit 0x10  bMESH
               Bits 0x0F max_hop
Byte  6..n   "<node_call>><dst>:"          ASCII, z. B. "DK5EN-90>232:"
Byte  n+1..  Meldungstext                  z. B. "QRT - stopping to accept new messages, ..."
```

Konkretes Beispiel, Node `DK5EN-90`, Gruppe `232`:

```
DK5EN-90>232:QRS - slow down, TX buffer is filling
DK5EN-90>232:QRT - stopping to accept new messages, TX buffer full
DK5EN-90>232:QTA - message discarded, TX buffer full
DK5EN-90>232:QRV - ready again, TX buffer clear
```

### Erkennung in der App

`msg_app_offline` (Bit `0x20` in Byte 5) ist gesetzt. Das Flag allein reicht als Merkmal
**nicht** — es steht auch auf relayten Frames und auf Kommandoantworten. Brauchbar ist erst
die Kombination:

1. `msg_app_offline` gesetzt, **und**
2. Absender gleich dem eigenen Node-Rufzeichen, **und**
3. Nutztext beginnt mit `QRS - `, `QRT - `, `QTA - ` oder `QRV - ` — nach BP-07 zusaetzlich
   `QRT NOT SENT - ` und `QTA NOT SENT - ` (Kapitel 9).

Das Textpraefix ist heute das einzige belastbare Unterscheidungsmerkmal. Das ist eine bekannte
Schwaeche (Luecke L5, Kapitel 8) und bleibt auch nach BP-07/08/09 bestehen.

Ein zweiter Hinweis, aber kein Beweis: bei einer Meldung ist `msg_id` schlicht `millis()`,
waehrend eine echte eigene Nachricht `msg_id = (GW_ID & 0x3FFFFF) << 10 | Zaehler` traegt,
wobei der Zaehler 0..999 laeuft. Die Wertebereiche ueberschneiden sich, das taugt nur als
Plausibilitaetspruefung.

---

## 6. Drahtformat EXTUDP

Bewusst genau wie eine ueber LoRa empfangene Textnachricht geformt, inklusive der numerischen
Firmwareversion (`35`, nicht der String `"4.35"`). Ein eigener Typ `notice` war die erste
Fassung dieses Pfades und in McApp schlicht unsichtbar — die App rendert nur die Formen, die
sie kennt.

```json
{
  "src_type": "lora",
  "type": "msg",
  "src": "DK5EN-90",
  "dst": "232",
  "msg": "QRT - stopping to accept new messages, TX buffer full",
  "msg_id": "0001A2B3",
  "firmware": 35,
  "fw_sub": "p",
  "rssi": 0,
  "snr": 0
}
```

`msg_id` ist `millis()` als achtstelliger Hexstring. `rssi` und `snr` sind 0 — auch das ein
Erkennungsmerkmal, aber kein sicheres.

---

## 7. Durchgespielt: 13 Nachrichten in einen 10er-Ring

Testaufbau: Node `DK5EN-90`, Entwickler-Build mit `MAX_RING 10`, also QRS bei 5 und QRT bei 8.
Dreizehn Nachrichten "Test 01" bis "Test 13" aus der App ueber BLE in Gruppe 232, im Abstand
von 300 ms. Der Funk schafft in dieser Zeit nichts abzuarbeiten. `node_msgid` startet bei 100.

| Nr. | t (ms) | msg\_id    | Tiefe danach | Im Ring  | Meldung |
| --- | ------ | ---------- | ------------ | -------- | ------- |
| 01  | 100000 | `1A2BC064` | 1            | ja       | —       |
| 02  | 100300 | `1A2BC065` | 2            | ja       | —       |
| 03  | 100600 | `1A2BC066` | 3            | ja       | —       |
| 04  | 100900 | `1A2BC067` | 4            | ja       | —       |
| 05  | 101200 | `1A2BC068` | 5            | ja       | **QRS** |
| 06  | 101500 | `1A2BC069` | 6            | ja       | —       |
| 07  | 101800 | `1A2BC06A` | 7            | ja       | —       |
| 08  | 102100 | `1A2BC06B` | 8            | ja       | **QRT** |
| 09  | 102400 | —          | 8            | **nein** | —       |
| 10  | 102700 | —          | 8            | **nein** | —       |
| 11  | 103000 | —          | 8            | **nein** | —       |
| 12  | 103300 | —          | 8            | **nein** | —       |
| 13  | 103600 | —          | 8            | **nein** | —       |

Serielle Konsole:

```
[BP];notice;QRS;depth;5;max;10;ms;101200
[BP];notice;QRT;depth;8;max;10;ms;102100
[BP];refuse;depth;8;max;10;ms;102400
[BP];refuse;depth;8;max;10;ms;102700
[BP];refuse;depth;8;max;10;ms;103000
[BP];refuse;depth;8;max;10;ms;103300
[BP];refuse;depth;8;max;10;ms;103600
[BP];notice;QRV;depth;0;max;10;ms;128400
```

Was im Chat 232 der App steht:

```
>> Test 01
>> Test 02
>> Test 03
>> Test 04
>> Test 05
   DK5EN-90: QRS - slow down, TX buffer is filling
>> Test 06
>> Test 07
>> Test 08
   DK5EN-90: QRT - stopping to accept new messages, TX buffer full

   ... Test 09 bis Test 13: nichts. Kein Echo, keine Meldung.

   DK5EN-90: QRV - ready again, TX buffer clear
```

**Ergebnis: 8 von 13 gehen auf die Luft. 5 sind verloren. Fuer keine einzige davon kommt eine
Rueckmeldung, die sie benennt.**

Zwei Beobachtungen, die aus diesem Durchlauf folgen:

**Der Ring laeuft bei reinem Lokalverkehr nie ueber.** QRT bei 80 % greift vorher. QTA kann
deshalb nur auftreten, wenn unabweisbarer Relaisverkehr den Ring fuellt — siehe unten.

**Die QRT-Meldung gehoert zur letzten erfolgreich gesendeten Nachricht, nicht zur ersten
abgewiesenen.** Sie entsteht aus der Schwellenueberschreitung beim Einreihen von Test 08, nicht
aus der Abweisung von Test 09.

### Der QTA-Fall

Der Zustandsautomat tastet die Ringtiefe nur beim Senden ab; der Leerlauf-Poll erzeugt
ausschliesslich QRV. Fuellt also Relaisverkehr den Ring, waehrend der Operator schweigt,
bleibt der Automat auf QUIET. Die **erste** Nachricht des Operators wird deshalb nie
abgewiesen — sie laeuft bis in den Ringeintrag durch und wird dort verworfen.

Ring voll (10/10 mit Relaisframes gleicher oder hoeherer Prioritaet), Operator tippt "Test 01":

```
>> Test 01                                              <- Echo, sieht gesendet aus
   DK5EN-90: QTA - message discarded, TX buffer full    <- ohne Bezug zu Test 01
```

Die Nachricht erscheint im Chat wie erfolgreich gesendet. Message-ID ist verbraucht, der
Eintrag steht im Eigen-TX- und Dedup-Speicher, das Frame ist weg. Auf einem Gateway ging
vorher noch der `0x41`-Status `0x01` ("server reached") an die App.

---

## 8. Bekannte Luecken im aktuellen Stand

Diese Punkte sind verifiziert. **L1 bis L4 werden von BP-07/08/09 geschlossen**
(`docs/bp-l1-l4-impl-plan.md`), L5 bleibt vorerst offen. Solange die Aenderung nicht
ausgeliefert ist, sollte eine App nicht darauf bauen, ueber einen Verlust informiert zu werden.

| Nr.    | Luecke                                                            | Status                 |
| ------ | ----------------------------------------------------------------- | ---------------------- |
| **L1** | Eine abgewiesene Nachricht erzeugt nie eine App-sichtbare Meldung | geplant: BP-07         |
| **L2** | Die Meldung nennt die betroffene Nachricht nicht                  | geplant: BP-07         |
| **L3** | Bei QTA sieht die Nachricht erfolgreich aus                       | geplant: BP-08         |
| **L4** | Am T-Deck geht der getippte Text verloren                         | geplant: BP-09         |
| **L5** | Meldungen sind nur am Textpraefix erkennbar                       | **offen, kein Termin** |

**L1 — Eine abgewiesene Nachricht erzeugt nie eine App-sichtbare Meldung.**
`onRefuse()` ruft `latchIfHigher(BP_NOTICE_QRT)` auf. Der Abweisungszustand wird aber nur in
zwei Zweigen betreten, die beide zwingend auf QRT oder QTA latchen. Der Latch steht damit immer
schon auf mindestens QRT, wenn eine Abweisung stattfindet, und `latchIfHigher` liefert `NONE`.
Der Rueckgabewert von `onRefuse()` ist konstant `BP_NOTICE_NONE`. Der Test
`test_refusal_announces_once_per_episode` ist gruen, weil er genau 1 zaehlt — und diese 1
stammt aus dem `onSend()` davor, nicht aus einem der 20 `onRefuse()`-Aufrufe.

**L2 — Die Meldung nennt die betroffene Nachricht nicht.**
Es gibt keinen Bezug zwischen Meldung und Nachrichtentext oder Message-ID. Bei QRT existiert
die Message-ID gar nicht, weil bewusst keine verbraucht wird.

**L3 — Bei QTA sieht die Nachricht erfolgreich aus.**
Das Echo an die App geht raus, bevor der Ringeintrag ueberhaupt versucht wird. Die App zeigt
die Nachricht regulaer an; die QTA-Meldung kommt danach als eigenstaendige, unverknuepfte
Zeile.

**L4 — Am T-Deck geht der getippte Text verloren.**
`event_functions.cpp:721` leert das Eingabefeld unbedingt nach `sendMessage()` und schaltet auf
die Nachrichtenliste um. Bei Abweisung ist der Text weder in der Liste noch im Eingabefeld.
Ursache ist, dass `sendMessage()` `void` zurueckgibt — kein Aufrufer kann auf den Ausgang
reagieren.

**L5 — Meldungen sind nur am Textpraefix erkennbar.**
Kein eigenes Frameformat, kein Typfeld, kein Statusbyte. Eine Nutzernachricht, die zufaellig
mit `QRT - ` beginnt, ist von einer Meldung nicht zu unterscheiden. Das zu beheben braucht eine
Protokollaenderung und Abstimmung mit der App-Seite, siehe Kapitel 10.

---

## 9. Geplante Aenderung BP-07 / BP-08 / BP-09

Plan mit Dateien, Tests und Reihenfolge: `docs/bp-l1-l4-impl-plan.md`. Hier nur, was sich fuer
Nutzende und fuer die App aendert. Vier Festlegungen des Operators vom 2026-09-01 sind
eingearbeitet.

### Neu: eine Quittung pro verlorener Nachricht, in Echo-Form

Bisher gibt es nur ein Echo (angenommen) oder Stille (verloren). Neu tritt an die Stelle der
Stille eine Zeile in derselben Form wie das Echo, die den Nachrichtentext nennt und den Grund
als Q-Code voranstellt:

```
QRT NOT SENT - Hello World 17      (abgewiesen, Puffer im QRT-Band)
QTA NOT SENT - Hello World 17      (angenommen, dann vom Ring verworfen)
```

Die gelatchte Episodenwarnung bleibt daneben unveraendert: eine QRS und eine QRT pro Episode,
mit dem bisherigen Wortlaut. Sie ist die Vorwarnung, die NOT-SENT-Zeile ist die Quittung zur
einzelnen Nachricht. **Pro verlorener Nachricht genau ein Frame.**

### So sieht ein Verlauf aus

```
Senden: Grp 20, "Hello World 1"
Echo:   Grp 20, "Hello World 1"

... weitere Nachrichten ...

Senden: Grp 20, "Hello World 5"
Echo:   Grp 20, "Hello World 5"
Echo:   Grp 20, "QRS - slow down, TX buffer is filling"

... weitere Nachrichten ...

Senden: Grp 20, "Hello World 16"
Echo:   Grp 20, "Hello World 16"
Echo:   Grp 20, "QRT - stopping to accept new messages, TX buffer full"

Senden: Grp 20, "Hello World 17"
Echo:   Grp 20, "QRT NOT SENT - Hello World 17"

Senden: Grp 20, "Hello World 18"
Echo:   Grp 20, "QRT NOT SENT - Hello World 18"

... Puffer laeuft leer ...

Echo:   Grp 20, "QRV - ready again, TX buffer clear"
```

Aus stillen Verlusten werden benannte. Die Regel bleibt einfach: **auf jede gesendete Nachricht
kommt genau eine Zeile zurueck** — entweder das Echo oder die NOT-SENT-Quittung.

### Rahmung und Kuerzung

Die NOT-SENT-Zeile ist **kein echtes Echo**, sondern eine Meldung in Echo-Form: gerahmt wie die
bisherigen Q-Code-Meldungen (Kapitel 5 und 6), also `msg_app_offline = true` und
`msg_id = millis()`. Sie traegt bewusst **keine reale `msg_id`** — im Abweisungsfall existiert
gar keine, weil der Node fuer eine nicht gesendete Nachricht keine verbraucht.

Fuer die App heisst das: **nichts zu aendern**, das Frameformat ist bekannt. Und zugleich: die
Zuordnung laeuft weiter ueber den Text, nicht ueber eine Kennung (Luecke L5 bleibt).

Der Nachrichtentext wird auf **120 Byte** gekuerzt, bei Kuerzung mit `...` markiert, auf allen
Transportwegen gleich. Bindend ist dabei EXTUDP: das Datagramm hat rund 153 Byte Platz fuer den
Text, BLE haette 237. Ein einheitlicher Wert sorgt dafuer, dass App und externer Client
dasselbe sehen.

Drei Feinheiten, die beim Textvergleich in der App zaehlen:

- **120 Byte, nicht 120 Zeichen.** Bei reinem ASCII ist das dasselbe, bei deutschen Umlauten
  rund 105 sichtbare Zeichen, bei Emoji rund 30.
- Es wird nie mitten in eine UTF-8-Sequenz geschnitten.
- `"`, `\` und Steuerzeichen werden im gekuerzten Text durch Leerzeichen ersetzt, damit das
  EXTUDP-Datagramm beim JSON-Escapen nicht ueber seinen Puffer waechst.

### Neue Konsolenzeile

`[BP];notice;` bleibt Zustandswechseln vorbehalten, die Quittung bekommt einen eigenen Marker.
`txt;` steht als letztes Feld, weil der Nachrichtentext Semikolons enthalten kann:

```
[BP];refuse;depth;16;max;20;ms;102400
[BP];nack;QRT;dst;20;ms;102400;txt;Hello World 17
```

### QTA sieht nicht mehr nach Erfolg aus

Das Echo wandert hinter den Ringeintrag. Wird der Frame verworfen, kommt statt des Echos die
Quittung — die Nachricht erscheint also gar nicht erst als vermeintlich gesendet im Verlauf.

### Ein verworfener Frame geht auch nicht ins Backbone

Bisher lief der UDP-Uplink eines Gateways unabhaengig vom Ausgang des Ringeintrags: der
LoRa-Ring konnte den Frame verwerfen, die Nachricht ging trotzdem an den zentralen Server.
Operatorentscheidung 2026-09-01: **das ist unsymmetrisch. Was nicht ueber LoRa gesendet werden
kann, geht auch nicht ins Backbone.** Eine Nachricht betritt das Netz entweder ganz oder gar
nicht.

Fuer Betreiber eines Gateways heisst das: eine lokal getippte Nachricht, deren Frame der
TX-Ring verwirft, erscheint ab BP-08 nicht mehr im Netz. Der Absender bekommt
`QTA NOT SENT - <Text>` und weiss, dass er wiederholen muss. Bisher ging sie ins Backbone,
ohne dass die HF-Nachbarn sie hoerten und ohne dass es jemand erfuhr.

### Der getippte Text bleibt stehen

`sendMessage()` bekommt einen Rueckgabewert:

| Wert | Bedeutung                                    |
| ---- | -------------------------------------------- |
| `0`  | angenommen                                   |
| `-1` | abgewiesen (QRT)                             |
| `-2` | verworfen (QTA)                              |
| `-3` | ungueltig (Laenge, DM an eigenes Rufzeichen) |

Am T-Deck wird das Eingabefeld dadurch nur noch bei `0` geleert — der abgewiesene Text steht
weiter da und kann nach dem QRV erneut gesendet werden. Das Webinterface antwortet
`sendmessage refused` statt `sendmessage ok`.

---

## 10. Anforderungen an die Firmware aus App-Sicht

| Anforderung                                                                     | Status                |
| ------------------------------------------------------------------------------- | --------------------- |
| Rueckmeldung fuer **jede** abgewiesene Nachricht, nicht nur pro Zustandswechsel | BP-07 deckt das ab    |
| Bezug zur betroffenen Nachricht                                                 | BP-07, ueber den Text |
| Kein erfolgreich aussehendes Echo fuer eine verworfene Nachricht                | BP-08 deckt das ab    |
| Getippter Text ueberlebt eine Abweisung                                         | BP-09 deckt das ab    |
| **Maschinenlesbares Merkmal fuer Rueckstaumeldungen**                           | **offen (L5)**        |
| **Kennung statt Text, um die eigene Nachricht sicher zuzuordnen**               | **offen**             |

Die beiden offenen Punkte gehoeren zusammen und brauchen eine Entscheidung auf der App-Seite.

Die stabilere Variante fuer beide waere ein Statusframe statt einer Textmeldung: die Nachricht
normal echoen und direkt ein `0x41`-Frame mit einem neuen Statusbyte (etwa `0x03` = "not sent")
auf dieselbe `msg_id` schicken. Das verknuepft exakt die richtige Nachricht und passt in das
bestehende Statusschema der App (`0x01` server reached, `0x02` ACK). Preis: es verbraucht eine
Message-ID fuer etwas, das nie gesendet wurde — genau das, was BP-01 bewusst vermieden hat —
und ohne App-Unterstuetzung sieht der Nutzer gar nichts. Deshalb ist es nicht Teil von
BP-07/08/09: die Textvariante wirkt sofort und ohne App-Release.

---

## 11. Empfohlene Flusssteuerung in der App

### Mit dem heutigen Stand

**QRS erkannt:** Sendetaste nicht sperren, aber einen dezenten Hinweis einblenden ("Node sendet
gerade viel"). Wer jetzt weitertippt, kommt noch durch.

**QRT erkannt:** Sendetaste sperren oder deutlich warnen. Ab hier werden neue Nachrichten
abgewiesen, und der Nutzer bekommt das heute nicht mit. Getippten Text **nicht** aus dem
Eingabefeld loeschen, bis ein Echo eingetroffen ist.

**QTA erkannt:** Die zuletzt gesendete, noch unbestaetigte Nachricht als unzugestellt
markieren. Das ist eine Heuristik, kein Beweis — es gibt heute keinen Bezug.

**QRV erkannt:** Sperre aufheben.

**Unabhaengig von den Meldungen: ein Echo-Timeout.** Bleibt das Echo einer gesendeten Nachricht
laenger als wenige Sekunden aus, ist sie mit hoher Wahrscheinlichkeit abgewiesen worden. Das
ist derzeit der einzige zuverlaessige Weg, eine abgewiesene Nachricht in der App zu erkennen.

### Nach BP-07/08/09

Der Echo-Timeout bleibt als Rueckfallebene sinnvoll, ist aber nicht mehr die Hauptquelle. Neu
moeglich:

**Text abgleichen.** Eine Rueckmeldung mit dem Praefix `QRT - not sent` oder `QTA - not sent`
nennt den Anfang des Nachrichtentextes. Die App kann ihn gegen ihre eigenen noch
unbestaetigten Nachrichten in diesem Chat abgleichen und genau die als unzugestellt markieren —
mit Wiederholen-Knopf statt einer allgemeinen Warnung. Auf die 48-Zeichen-Kuerzung und das
angehaengte `...` achten: der Vergleich muss ein Praefixvergleich sein, kein Gleichheitstest.

**Sperre praeziser setzen.** Nach der ersten Rueckmeldung ist sicher, dass Nachrichten verloren
gehen — nicht nur wahrscheinlich. Die Sendetaste kann bis zum QRV gesperrt und der Verlauf mit
einer Wiederholen-Aktion versehen werden.

**Nicht automatisch wiederholen.** Ein automatischer Neuversuch waehrend einer laufenden
QRT-Episode fuellt den Ring erneut, sobald er sich lockert. Wiederholen gehoert hinter das QRV
und hinter eine bewusste Nutzeraktion.

---

## Anhang: Markerreferenz fuer die Logauswertung

| Marker                                       | Bedeutung                                                       |
| -------------------------------------------- | --------------------------------------------------------------- |
| `[BP];notice;<code>;depth;N;max;M;ms;T`      | Zustandswechsel, eine Zeile pro Episode/Code                    |
| `[BP];refuse;depth;N;max;M;ms;T`             | Eine Nachricht wurde abgewiesen                                 |
| `[BP];nack;<code>;dst;D;ms;T;txt;<Text>`     | Quittung zu einer verlorenen Nachricht — **geplant, BP-07**     |
| `[MC-DBG] RING_DROP_NEW slot=...`            | Ring hat den neuen Frame verworfen (QTA-Quelle)                 |
| `[MC-DBG] RING_DROP_PRIO slot=...`           | Ring hat einen aelteren Frame niedrigerer Prioritaet verdraengt |
| `[MC-DBG] RING_STATUS ... queued=N/M dist=D` | Ringtiefe (belegte Plaetze) und alte Indexdistanz               |

Die `[BP];`-Zeilen sind bewusst unbedingtes `Serial.printf` und kein `DEBUG_MSG` — sie
erscheinen auch mit abgeschaltetem Debug, damit die Bench sie unabhaengig vom Transport
pruefen kann.
