# ACK mit Absender: Wer hat quittiert?

Stand 2026-09-05, Codebasis fork-main (`ffe31ca5`). Analyse, kein Code.

Stephans Wunsch: In der App soll bei der Wolke bzw. der Wolke mit Hakerl sichtbar sein, **wer**
quittiert hat. Dafuer muss man wissen, was der Node heute ueber den Quittierenden weiss, was er
davon an die App weitergibt, und was auf dem Draht ueberhaupt mitkommt.

## 1. Kurzfassung

- Der Frame zur App ist leicht erweiterbar. Der Node kennt den Absender aber nur in zwei von
  drei Faellen.
- **Heard** (ein Nachbar hat meinen Frame wiederholt): Rufzeichen liegt vor, nur Firmware
  noetig.
- **DM-ACK** (Wolke mit Hakerl): Absender ist der DM-Partner, den die App sowieso kennt. Neu
  waere nur: kam es ueber LoRa oder ueber den Server.
- **Gateway-ACK** (Wolke): Der 12-Byte-Binaerframe auf dem Draht traegt **kein** Rufzeichen und
  auch keinen Node-Hash. Das ist der interessante Fall, und er braucht eine Aenderung am
  Funkprotokoll, die auf allen Nodes ankommen muss.

## 2. Was die App heute bekommt

Jeder Statuswechsel geht als ein Frame ueber BLE, gebaut in `addBLEOutBuffer()`
(`src/loop_functions.cpp:571`). Der Node haengt hinter jedes Nicht-JSON-Frame vier Byte
Unix-Zeit an und schreibt die Gesamtlaenge in Byte 0 des Ringpuffers.

```
0x41 | msg_id (4 Byte, LE) | Status | 0x00        + Unix-Zeit (4 Byte, BE)

Status 0x00  heard          Nachbar hat meinen Frame wiederholt      (lora_functions.cpp:838)
Status 0x01  server reached Gateway-ACK, oder Node ist selbst GW      (lora_functions.cpp:364, loop_functions.cpp:4131)
Status 0x02  ACK            Empfaenger hat quittiert, nur DM          (lora_functions.cpp:992, udp_functions.cpp:406)
```

Byte 6 ist ein fixer Terminator. Ein Rufzeichen steht nirgends. Die App findet den Zeitstempel
vermutlich ueber einen festen Offset, das ist die "Auswertung in der App", die Stephan meint.

## 3. Was der Node ueber den Quittierenden weiss

| Status          | Absender bekannt? | Woher                                                                  | Bewertung                                  |
| --------------- | ----------------- | ---------------------------------------------------------------------- | ------------------------------------------ |
| 0x00 heard      | ja                | Pfad des wiederholten Frames, Firmware loggt schon `HEARD from <call>` | Firmware-only, wenige Zeilen               |
| 0x02 DM-ACK     | ja, aber trivial  | `msg_source_call` der Textmeldung `DEST:ack123`                        | Neu waere nur "via LoRa" vs. "via Server"  |
| 0x01 Gateway    | **nein**          | Binaerframe ohne Rufzeichen, eigene msg_id ist `millis()`              | Braucht Protokollerweiterung auf dem Draht |
| 0x01 eigener GW | entfaellt         | Node selbst ist GW mit IP, sendet sich den Status sofort               | Rufzeichen = eigenes                       |

Der Gateway-ACK-Frame auf dem Draht (`src/lora_functions.cpp:1210`):

```
Byte 0      0x41               MSG_TYPE_ACK
Byte 1..4   ack msg_id (LE)    = millis(), reine Dedup-Kennung
Byte 5      0x80 | max_hop     Bit 7 Server-Flag, Bit 0..6 Resthops
Byte 6..9   msg_id (LE)        die quittierte Nachricht
Byte 10     0x01               GW/Node, in der Praxis immer 0x01
Byte 11     0x00               Terminator
```

Anders als beim Text-ACK ist die eigene msg_id hier **nicht** nach dem Schema
`(GW_ID << 10) | counter` gebaut. Nicht einmal der 22-Bit-Hash des Gateways steckt drin.

Zwei Sperren begrenzen ausserdem, was die App sieht:

- Nur das **erste** Gateway-ACK geht zur App (`own_msg_id[..][4] < 2`,
  `src/lora_functions.cpp:364`). Weitere ACKs werden nur noch zum Retransmit-Stopp genutzt.
- Nur der **erste** Heard geht zur App (`== 0x00`, `src/lora_functions.cpp:838`).

Fuer eine Liste "wer hat quittiert" muessen beide Sperren fallen.

## 4. Vorschlag

### 4.1 Auf dem Draht (Gateway-ACK)

Das Rufzeichen des Gateways als Anhang hinter Byte 11, Byte 11 wird von "immer 0x00" zur
Laenge des Anhangs:

```
Byte 0..10   wie bisher
Byte 11      n = Laenge des Anhangs, 0 = altes Format
Byte 12..    Rufzeichen, n Byte, ohne NUL, Zeichen [A-Z0-9-], n <= 10
```

Neue Laenge 12..22 Byte statt fix 12.

### 4.2 Zur App (BLE)

Gleiches Muster: Byte 6 bleibt 0x00 als Trenner, danach das Rufzeichen. Der Zeitstempel bleibt
wie heute die letzten vier Byte des Frames. Damit die App das sauber lesen kann, muss sie den
Zeitstempel ueber `len - 4` lokalisieren statt ueber Offset 7.

Fuer Heard und DM-ACK ist das Rufzeichen sofort da. Fuer den Gateway-ACK kommt es erst, wenn das
sendende Gateway und alle Relays auf dem Weg die neue Firmware haben.

### 4.3 Reihenfolge

1. Firmware: Parser fuer den Anhang auf allen Nodes, toleriert altes und neues Format. Relays
   leiten die volle Laenge weiter. Noch kein Node sendet den Anhang.
2. App: Parser fuer den variablen Frame, Zeitstempel ueber `len - 4`.
3. Firmware: Heard und DM-ACK mit Rufzeichen zur App. Sichtbares Ergebnis fuer Stephan.
4. Firmware: Gateways senden den Anhang auf dem Draht. Sperren fuer Mehrfach-ACK lockern.

Das passt zu dem, was `docs/backpressure-protocol.md` fuer die fehlenden Zustaende (refused,
dropped, on air) sowieso vorschlaegt. Eine Frame-Revision, nicht zwei.

## 5. Was auf dem Draht nichts kaputt machen darf

### 5.1 Verhalten alter Firmware bei einem 22-Byte-ACK

Geprueft an fork-main, fuer upstream DEV noch zu verifizieren (siehe 5.7):

- `handleACK()` prueft `size < 12` und `payload[0] == 0x41`, dann `isPlausibleAckFrame()`.
  Die Plausibilitaet haengt nur an Byte 5. Ein laengerer Frame **besteht** alle Pruefungen.
- `memcpy(print_buff, payload, 12)` kopiert genau 12 Byte. Der Anhang wird ignoriert, nicht
  fehlinterpretiert.
- Dedup laeuft ueber Byte 1..4. Unveraendert, alte und neue Nodes sehen denselben Frame als
  denselben.
- Retransmit-Stopp und Statusmeldung zur App laufen ueber Byte 6..9. Unveraendert.
- Relay: `print_buff[5]--` und `addTxRingEntry(print_buff, 12, ...)`. Ein alter Relay
  **kuerzt** das ACK auf 12 Byte. Das Rufzeichen geht auf diesem Hop verloren, das ACK selbst
  kommt an.

Ergebnis: Alte Nodes verhalten sich mit dem neuen Frame exakt wie heute. Das Schlimmste ist ein
fehlendes Rufzeichen, nie ein verlorenes ACK.

### 5.2 Was der neue Parser nie tun darf

- Ein ACK wegen eines kaputten Anhangs verwerfen. Der Anhang ist Zusatzinformation. Ungueltige
  Laenge, Zeichen ausserhalb `[A-Z0-9-]`, `n > 10` oder `12 + n > size`: Anhang verwerfen,
  Frame als 12-Byte-ACK weiterverarbeiten.
- Byte 10/11 als Verwerfungsgrund nutzen. Die Feldmessung in `src/ack_functions.h` zeigt
  0,4 Prozent gueltiger ACKs mit abweichenden Werten dort, vermutlich aeltere Firmware. Ein
  Byte 11 ungleich 0 ohne passenden Anhang muss daher als "altes Format" gelten, nicht als
  Fehler.
- Den Anhang aus einem Puffer lesen, der kuerzer ist als `12 + n`. Die Laengenpruefung kommt
  vor jedem Zugriff.

### 5.3 Byte 5 bleibt der Diskriminator

`isPlausibleAckFrame()` bleibt unveraendert. Sie hat den 'A'-Bruchstueck-Befall im Feld beendet
(506 Fremdframes in 32,7 h). Der Anhang darf diese Pruefung nicht aufweichen und nicht ersetzen.

### 5.4 Airtime

Ein Gateway-ACK waechst von 12 auf bis zu 22 Byte Payload, also auf das Doppelte des Nutzteils.
Im DG0OPK-Korpus liefen etwa 90 ACK-Frames pro Node und Stunde. Vor einem Rollout die
zusaetzliche Airtime mit den echten Funkparametern rechnen und gegen die Kanalauslastung
der `--setlog`-Messung halten. Falls zu teuer: statt Rufzeichen den 22-Bit-Node-Hash
(3 Byte) senden und die Aufloesung Hash zu Rufzeichen der App oder dem Server ueberlassen.
Die App sieht jede Textmeldung mit `msg_id` und Absender und kann die Tabelle selbst lernen.

### 5.5 Mehrfach-ACK zur App

Faellt die Sperre "nur das erste ACK", bekommt die App pro Nachricht mehrere Frames mit Status
0x01. Eine alte App, die den Status als Zustandsmaschine fuehrt, verkraftet das vermutlich
(gleicher Zustand nochmals gesetzt). Trotzdem: erst freischalten, wenn die App den Frame
versteht. Am saubersten ueber ein Faehigkeitsflag der App beim Verbinden, sonst ueber die
Firmware-Version der App, wenn die im Handshake mitkommt. Zu pruefen, was der Handshake heute
hergibt.

### 5.6 Server bleibt unberuehrt

ACK-Frames erreichen den Server nicht. `handleACK()` kehrt bei `src/lora_functions.cpp:530`
zurueck, `addNodeData()` (Uplink zum Server) wird erst bei Zeile 1360 gerufen. Der neue Anhang
kann also keinen Server-Parser treffen. Gleiches gilt fuer die UDP-Rueckrichtung, dort kommt
das ACK als Text `:ack123`, nicht binaer.

### 5.7 Vor der Umsetzung pruefen

- **Upstream DEV** `handleACK()`: hat upstream die `size < 12`-Pruefung und den 12-Byte-memcpy
  ebenfalls, oder kopiert dort etwas die volle Laenge in einen kleinen Puffer? Nur dann waere
  ein 22-Byte-ACK auf alten Nodes gefaehrlich. In fork-main ist es sicher.
- **App-Parser**: fester Offset fuer den Zeitstempel oder `len - 4`? Bei festem Offset zeigt
  eine alte App das Rufzeichen als Zeit an.
- **Puffergroessen** in der Kette `print_buff[30]`, `BLEtoPhoneBuff`, Ring-Slot. 22 Byte plus
  Anhang plus Zeit passt ueberall, trotzdem mit dem Laengenbyte (uint8_t) gegenrechnen.
- **Ringpuffer-Slot 12 vs. size**: Der Relay muss `size` statt der Konstante 12 weiterreichen,
  aber nach oben auf 22 klemmen.

### 5.8 Nachweis

- `test/test_ack_validate` (nativ) um Anhang-Faelle erweitern: n = 0, n = 6, n = 10, n = 11,
  Sonderzeichen, `12 + n > size`, Byte 11 ungleich 0 ohne Anhang. Jeder Fall muss das ACK
  akzeptieren, nur der Anhang darf fallen.
- `test/test_ack_replay` mit einem aufgezeichneten 22-Byte-ACK.
- Bench: ein Node alte Firmware, einer neue. Neue Firmware sendet 22-Byte-ACK, alter Node
  muss Retransmit stoppen und Status 0x01 zur App geben. Umgekehrt: alter Node sendet
  12 Byte, neuer Node darf keinen Anhang halluzinieren. Die `[LOG]`-Zeilen zeigen die
  Frame-Laenge, das reicht als Instrument.

## 6. extUDP

### 6.1 Was heute existiert

Ausgehend (`sendExtern()`, `src/extudp_functions.cpp:445`), jeweils als JSON-Datagramm:

| Typ    | Ausloeser                                 | Kennzeichnung                            |
| ------ | ----------------------------------------- | ---------------------------------------- |
| `pos`  | jedes Positionsframe (LoRa, UDP, eigenes) | `src_type` lora / udp / node, `msg_id`   |
| `msg`  | jedes Textframe (LoRa, UDP, eigenes)      | `src`, `dst`, `msg`, `msg_id`            |
| `tele` | zu jedem Positionsframe zusaetzlich       | qfe, qnh, pressure_alt                   |
| Notice | QRS/QRT/QTA-Rueckmeldung (BP-01, BP-06)   | als `msg` vom eigenen Rufzeichen getarnt |

Eingehend (`getExtern()`, Zeile 239): `msg` (dst, msg) und `tele`. Sonst nichts.

Nuetzlich fuer den Peer: Eine ueber extUDP eingespeiste Nachricht wird als `src_type:"node"`
mit ihrer `msg_id` zurueckgespiegelt (`src/loop_functions.cpp:4181`). Der Peer kann seine
Nachricht also der msg_id zuordnen. DMs bekommen dabei automatisch die ACK-Anforderung
`{nnn}` (`src/loop_functions.cpp:4027`).

### 6.2 Was fehlt

- **Kein Zustellstatus.** Der 0x41-Frame erreicht extUDP nie: `handleACK()` kehrt bei Zeile 530
  zurueck, `queueExtern()` wird erst bei Zeile 890 gerufen, und `sendExtern()` wuerde ein
  Binaerframe ohnehin verwerfen (`decodeAPRS()` liefert 0). Weder Heard noch Server-reached
  noch DM-ACK kommen als Status beim Peer an.
- **DM-ACK nur als Text.** Das Text-ACK `DEST:ack123` ist ein normales 0x3A-Frame und geht als
  `msg` raus. Der Peer muss `:ack` selbst erkennen und die dreistellige Nummer ueber die
  msg_id-Regel `(GW_ID << 10) | ack_id` selbst auf seine Nachricht zurueckrechnen.
- **Notice als getarnte Nachricht.** Bewusste Entscheidung vom 2026-08-31, weil McApp nur die
  bekannten Formen rendert. Ein Peer kann eine Notice nicht maschinell von einer Meldung
  unterscheiden.
- **Kein Heard.** Der Peer erfaehrt nicht, ob und von wem seine Nachricht wiederholt wurde.

### 6.3 Was zu ergaenzen waere

Ein Status-Datagramm, das den BLE-Frame spiegelt und dort abgesetzt wird, wo heute
`addBLEOutBuffer(print_buff, 7)` fuer 0x41 gerufen wird:

```json
{
  "type": "ack",
  "msg_id": "1A2B3C4D",
  "status": 1,
  "from": "OE1XYZ-12",
  "via": "lora"
}
```

- `status` 0 heard, 1 server reached, 2 ack, spaeter 3..6 wie im Backpressure-Vorschlag.
- `from` das Rufzeichen, wo bekannt, sonst leer. Kommt fuer den Gateway-ACK erst mit dem
  Anhang aus Abschnitt 4.1.
- `via` lora oder udp, weil der Node das beim DM-ACK unterscheiden kann.
- Testbar wie `extern_notice_json.h`: reine Funktion im Header, nativer Test daneben.

Kein Absetzen aus `OnRxDone` heraus, sondern ueber `queueExtern()` in die Hauptschleife, aus
demselben Grund wie heute bei den Textframes. Die Queue hat zwei Plaetze, mit Status-Frames
wird sie enger. Vor der Umsetzung `MAX_EXTERN_QUEUE` gegen die erwartete Rate halten.

Alle drei Stufen (Heard, DM-ACK mit Absender, Gateway-ACK) kommen beim Peer dann im selben
Datagramm an, ohne dass extUDP fuer die Gateway-Erweiterung ein zweites Mal angefasst wird.
